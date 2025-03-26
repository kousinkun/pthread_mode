#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define BUffER_size 10                          //共有データ数量

typedef struct {
    int temperature;
    int humidity;
    int light;
} SensorData;

SensorData buffer[BUffER_size];                 //共有バッファ

int count = 0;                                  //バッファデータ数量を計数

pthread_mutex_t mutex;
pthread_cond_t cond_producer, cond_consumer;

//センサーの温度、湿度、照度をシミュレートする//
SensorData read_sensor_data(){
    SensorData data;
    data.temperature = rand() % 40;
    data.humidity = rand() % 100;
    data.light = rand() % 1000;

    return data;
}
//プロデューサースレッド：センサーデータを収集する
void* producer(void* arg){
    while(1){
        //排他ロック
        pthread_mutex_lock(&mutex);
        //bufferをいっぱいの時に、止まる、スレッドCの呼び出すを待っている。
        while(count == BUffER_size){
            pthread_cond_wait(&cond_producer,&mutex);
        };

        SensorData data = read_sensor_data();
        buffer[count++] = data;
        printf("数据采集:温度=%d c, 湿度= %d %%，光照= %d  Lux\n",
        data.temperature, data.humidity, data.light);

        //データをbufferにアップロードしたら、スレッドCを呼び出す。
        pthread_cond_signal(&cond_consumer);
        //排他アンロック
        pthread_mutex_unlock(&mutex);

        sleep(1);    
    }
    return NULL;
}
//コンシューマースレッド：データを出力する
void* consumer(void* arg){
    while(1){
        //排他ロック
        pthread_mutex_lock(&mutex);
        //bufferを空っぽの時に、止まる、スレッドPの呼び出すを待っている。
        while(count == 0){
            pthread_cond_wait(&cond_consumer, &mutex);
        }

        SensorData data = buffer[--count];
        printf("出力:温度=%d c, 湿度= %d %%，光照= %d  \n",
        data.temperature, data.humidity, data.light);

        //データを取り出したら、スレッドPを呼び出す
        pthread_cond_signal(&cond_producer);
        //
        pthread_mutex_unlock(&mutex);        
        sleep(1);
    }
    return NULL;
}

int main(){
    pthread_t producer_thread, consumer_thread;

    pthread_mutex_init(&mutex,NULL);
    pthread_cond_init(&cond_consumer,NULL);
    pthread_cond_init(&cond_producer,NULL);

    //タイマー

    time_t start_time = time(NULL);
    int run_duration = 20;             //実行時間を設定

    pthread_create(&producer_thread,NULL,producer,NULL);
    pthread_create(&consumer_thread,NULL,consumer,NULL);

    //実行時間を監視
    while(1){
        time_t current_time = time(NULL);
        if(difftime(current_time,start_time) >= run_duration){
            printf("プロセスも実行 %d 秒，終了します。\n", run_duration);

            break;
        }
        sleep(1);
    }
    //キャンセ信号をスレッドに送る
    pthread_cancel(producer_thread);
    pthread_cancel(consumer_thread);
    //スレッド回収する
    pthread_join(producer_thread,NULL);
    pthread_join(consumer_thread,NULL);
    //ロックと条件ロックを削除
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond_producer);
    pthread_cond_destroy(&cond_consumer);

    return 0;
}