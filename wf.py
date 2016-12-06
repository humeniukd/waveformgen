#!/usr/bin/python

import os, logging.handlers
import boto.sqs as sqs
import time, subprocess, json, threading
import boto.s3 as s3
from boto.sqs.message import RawMessage
from boto.s3.key import Key

LOG_FILE = '/tmp/wf.log'
WORK_DIR = '/tmp/'
AWS_ACCESS_KEY_ID = ''
AWS_SECRET_ACCESS_KEY = ''
MAX_THREADS = 4
HEIGHT = 140
WIDTH = 1800
WIDTH_SMALL = 800
QUEUE_NAME = ''
IN_BUCKET_NAME = ''
OUT_BUCKET_NAME = ''

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

# Handler
handler = logging.handlers.RotatingFileHandler(LOG_FILE, maxBytes=1048576, backupCount=5)
handler.setLevel(logging.DEBUG)

# Formatter
formatter = logging.Formatter('%(asctime)s - %(threadName)s - %(levelname)s - %(message)s')

# Add Formatter to Handler
handler.setFormatter(formatter)

# add Handler to Logger
logger.addHandler(handler)

lock = threading.Lock()
count = 0

class S3Message(RawMessage):

    def encode(self, value):
        return value

    def decode(self, value):
        try:
            value = json.loads(value)
            value = value['Records'][0]['s3']['object']['key']
        except:
            logger.warning('Unable to decode message %s', value)
            return None
        return value

class WfThread(threading.Thread):
    __SQSQueue = None
    __S3Conn = None

    @property
    def SQSQueue(self):
        if None == self.__SQSQueue:
            self.__SQSQueue = sqs.connect_to_region("eu-central-1", aws_access_key_id=AWS_ACCESS_KEY_ID,
                                                    aws_secret_access_key=AWS_SECRET_ACCESS_KEY).create_queue(self.__key)
        return self.__SQSQueue

    @property
    def S3Conn(self):
        if None == self.__S3Conn:
            self.__S3Conn = s3.connect_to_region("eu-central-1", aws_access_key_id=AWS_ACCESS_KEY_ID,
                                     aws_secret_access_key=AWS_SECRET_ACCESS_KEY)
        return self.__S3Conn

    def __init__(self, key=None):
        global count
        self.__key = key
        self.__outFile = key + '.mp3'
        self.__sJsonFile = key + '_s.json'
        self.__mJsonFile = key + '_m.json'
        lock.acquire()
        count += 1
        lock.release()
        threading.Thread.__init__(self, name=self.__key)

    def __del__(self):
        global count
        lock.acquire()
        count -= 1
        lock.release()
        for fileName in [self.__sJsonFile, self.__mJsonFile, self.__key, self.__outFile]:
            os.remove(WORK_DIR + fileName)
        logger.debug('Destruct %s', self.__key)

    def _set_daemon(self):
        return True

    def run(self):
        for fn in [self.__download, self.__process, self.__upload]:
            if not fn():
                self.__enqueue('{"key": "error"}')
                break

    def __process(self):
        logger.debug('Processing')
        process = subprocess.Popen([
            'wf',
            '-i', WORK_DIR + self.__key,
            '-o', WORK_DIR + self.__outFile,
            '-h', str(HEIGHT),
            '-W', str(WIDTH),
            '-w', str(WIDTH_SMALL)
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=1)
        while True:
            output = process.stdout.readline()

            if output == '' and process.poll() is not None:
                break

            if output:
                output = int(output.strip())

                if 100 < output:
                    type = 'duration'
                else:
                    type = 'percent'
                logger.debug('Reading output: %s - %d', type, output)
                self.__enqueue('{"type": "%(key)s", "value": "%(value)s"}' % {'key': type, 'value': output})

        rc = process.poll()
        logger.debug('RC: %d', rc)
        return 0 == rc

    def __enqueue(self, msg):
        m = RawMessage()
        m.set_body(msg)
        return self.SQSQueue.write(m)

    def __download(self):
        bucket = self.S3Conn.get_bucket(IN_BUCKET_NAME)
        obj = bucket.get_key(self.__key)
        if None == obj:
            logger.debug('No key to download %s', self.__key)
            return False
        try:
            obj.get_contents_to_filename(WORK_DIR + self.__key)
        except Exception as e:
            logger.debug('Download failed %s', str(e))
            return False
        logger.debug('Downloaded %s', self.__key)
        return True

    def __upload(self):
        for fileName in [self.__sJsonFile, self.__mJsonFile, self.__outFile]:
            bucket = self.S3Conn.get_bucket(OUT_BUCKET_NAME)
            k = Key(bucket, fileName)
            try:
                k.set_contents_from_filename(WORK_DIR + fileName)
                k.set_acl('public-read')
            except Exception as e:
                logger.debug('Upload failed: %s', str(e))
            logger.debug('Uploaded %s', fileName)
        return True

def main():
    conn = sqs.connect_to_region("eu-central-1", aws_access_key_id=AWS_ACCESS_KEY_ID, aws_secret_access_key=AWS_SECRET_ACCESS_KEY)
    mainQueue = conn.get_queue(QUEUE_NAME)
    mainQueue.message_class = S3Message
    while 1:
        time.sleep(2)
        if MAX_THREADS < count:
            logger.debug('Queue is full')
            continue
        rs = mainQueue.get_messages()
        for m in rs:
            WfThread(m.get_body()).start()
            m.delete()

main()