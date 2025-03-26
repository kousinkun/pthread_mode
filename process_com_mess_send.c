#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>


#define BUffER_size 10                                      //共有データ数量

typedef struct {
    int temperature;
    int humidity;
    int light;
} SensorData;

//共有リソース

struct msgbuf                                               //共享mess buffer
{
    long mtype;
    char messdata[256];  //消息正文
};
//---------------------------------------

int msgid;                                                  //共有mess key
key_t key = 1234;                                           //KEY
SensorData buffer[BUffER_size];                             //スレッド共有バッファ
int count = 0;                                              //バッファデータ数量を計数
//----------------------------------------
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
        pthread_testcancel();                               // ✅ 允许 pthread_cancel 生效
        pthread_mutex_lock(&mutex);                         //排他ロック
        
        while(count == BUffER_size){                        //bufferをいっぱいの時に、止まる、スレッドCの呼び出すを待っている。
            pthread_cond_wait(&cond_producer,&mutex);
        };

        SensorData data = read_sensor_data();
        buffer[count++] = data;
        printf("データ収集:温度=%dc, 湿度= %d%%，光照= %d  \n",
        data.temperature, data.humidity, data.light);

        pthread_cond_signal(&cond_consumer);                 //データをbufferにアップロードしたら、スレッドCを呼び出す。
        pthread_mutex_unlock(&mutex);                       //排他アンロック

        sleep(5);    
    }
    return NULL;
}
//コンシューマースレッド：データをメッセージキューにアップロード
void* consumer(void* arg){
    
    struct msgbuf msg;                                      //msgbuf構造体を取得し、メッセージキューのバッファを作成する。

    while(1){
        pthread_testcancel();                               // ✅ 允许 pthread_cancel 生效
        pthread_mutex_lock(&mutex);                         //排他ロック

        while(count == 0){                                  //bufferが空っぽの時に、止まるスレッドPの呼び出すを待っている。
            pthread_cond_wait(&cond_consumer, &mutex);
        }
        SensorData data = buffer[--count];


//-------------------------------------------------------------------------------------
        //バッファの最初の変数を1に設定し、データを編集して送信の準備をする。
        msg.mtype = 1;
        memcpy(msg.messdata,&data, sizeof(SensorData));
        //"メッセージキューにデータをアップロード
        if(msgsnd(msgid,&msg,sizeof(SensorData), 0) == -1){

            perror("msgsnd failed");
            return NULL;
        }
//--------------------------------------------------------------------------------------

        printf("メッセージキューにデータをアップロードした:温度=%dc, 湿度= %d %%，光照= %d  \n",
        data.temperature, data.humidity, data.light);

        pthread_cond_signal(&cond_producer);                //データをbufferにアップロードしたら、スレッドCを呼び出す。
        pthread_mutex_unlock(&mutex);                       //排他アンロック
        sleep(1);
    }
    return NULL;
}

int main(){
    //获取
    pthread_t producer_thread, consumer_thread;
    key_t key = ftok("/tmp",65);                                //キーを作成して、メッセージキューを生成する。
    if (key == -1) {
        perror("ftok failed");
        exit(1);
    }
    //初期化
    pthread_mutex_init(&mutex,NULL);
    pthread_cond_init(&cond_consumer,NULL);
    pthread_cond_init(&cond_producer,NULL);
    //------------------messを作る----------------------
    
    msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget failed");
        printf("可能的原因: 1) IPC 资源已满，2) 权限不足，3) ftok 错误\n");
        exit(1);
    }

    //-----------------タイマー-------------------------
    
    time_t start_time = time(NULL);
    int run_duration = 20;                                      //実行時間を設定

    //---------------スレッドを作る---------------------
    pthread_create(&producer_thread,NULL,producer,NULL);
    pthread_create(&consumer_thread,NULL,consumer,NULL);
    //-----------------------------------------------------------    
                                                                //実行時間を監視
    while(1){
        time_t current_time = time(NULL);
        if(difftime(current_time,start_time) >= run_duration){
            printf("プロセスも実行 %d 秒，終了します。\n", run_duration);

            break;
        }
        sleep(1);
    }
    //-----------------------------------------------------------
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