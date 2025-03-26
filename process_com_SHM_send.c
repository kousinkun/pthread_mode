#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>


#define BUffER_size 10                                       //共有データ数量

typedef struct {
    int temperature;
    int humidity;
    int light;
} SensorData;


//共享リソース
//共享内存的数据结构

typedef struct {
    SensorData buffer[BUffER_size];   // リングバッファ
    int write_index;                  // 書き込みインデックス
    int read_index;                   // 読み込みインデックス
} SharedMemory;

//---------------------------------------

int shmid;                                                     //共有SHM key
key_t key = 1234;                                               //KEY（制作一个钥匙来制作信息量集
SensorData buffer[BUffER_size];                             //スレッド共有バッファ
int count = 0;                                              //バッファデータ数量を計数
//----------------------------------------
pthread_mutex_t mutex;                                          //钥匙制作
pthread_cond_t cond_producer, cond_consumer;

//センサーを模擬して温度、湿度、照度を再現する//
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

        pthread_testcancel();                                   // ✅ 允许 pthread_cancel 生效
        pthread_mutex_lock(&mutex);                              //排他ロック

        while(count == BUffER_size){                            //bufferをいっぱいの時に、止まる、スレッドCの呼び出すを待っている。
            pthread_cond_wait(&cond_producer,&mutex);
        };

        SensorData data = read_sensor_data();
        buffer[count++] = data;
        printf("データ収集:温度=%dc, 湿度= %d%%，光照= %d  \n",
        data.temperature, data.humidity, data.light);

        pthread_cond_signal(&cond_consumer);                 //データをbufferにアップロードしたら、スレッドCを呼び出す。
        pthread_mutex_unlock(&mutex);                       //排他アンロック

        sleep(4);    
    }
    return NULL;
}
//コンシューマースレッド：データを共有メモリにアップロード
void* consumer(void* arg){

    //映射と解除映射が繰り返され、パフォーマンスに影響を与える。
    SharedMemory *shared_mem = (SharedMemory *)shmat(shmid, NULL, 0);
    if (shared_mem == (void *)-1) {
        perror("shmat failed");
        pthread_exit(NULL);
    }

    while(1){
        pthread_testcancel();                                   // ✅ 允许 pthread_cancel 生效
        pthread_mutex_lock(&mutex);                              //排他ロック

        while(count == 0){                                      //bufferを空っぽの時に、止まる、スレッドｐの呼び出すを待っている。
            pthread_cond_wait(&cond_consumer, &mutex);
        }
        
        //ここから、アップロードや通信などの操作を行うことができます。
//-------------------------------------------------------------------------------------
        // **バッファからデータを取得する**
        SensorData data = buffer[--count];
        // **共有メモリに書き込む**
        shared_mem->buffer[shared_mem->write_index] = data;
        // **データを表示する**
        printf("共有メモリにデータをアップロード: [write_index=%d] 温度=%d°C, 湿度=%d%%, 光照=%d \n",
            shared_mem->write_index, 
            shared_mem->buffer[shared_mem->write_index].temperature, 
            shared_mem->buffer[shared_mem->write_index].humidity, 
            shared_mem->buffer[shared_mem->write_index].light);
        // **shm_indexを更新する**
        shared_mem->write_index = (shared_mem->write_index + 1) % BUffER_size;  


//--------------------------------------------------------------------------------------
        pthread_cond_signal(&cond_producer);                        //データをbufferにアップロードしたら、スレッドCを呼び出す。
        pthread_mutex_unlock(&mutex);                                //排他アンロック        
        sleep(1);
    }
    
    shmdt(shared_mem);                                                      // 解除映射
    return NULL;
}

int main(){
    //获取ロックID
    pthread_t producer_thread, consumer_thread;
    //制作一个钥匙来制作信息量集
    key_t key = ftok("/tmp",65);
    if (key == -1) {
        perror("ftok failed");
        exit(1);
    }
    //--------------共有メモリの識別子を作成する。------------------
    shmid = shmget(key, sizeof(SharedMemory),IPC_CREAT|0666);               //10個SensorData　sizeのメモリ。
    if(shmid == -1){
        perror("共有メモリ作る失敗");
        exit(EXIT_FAILURE);
    }
    //共有メモリをプロセス空間のアドレスにアタッチして、初期化の準備をする。
    SharedMemory *shared_mem = (SharedMemory *)shmat(shmid, NULL, 0);
    if (shared_mem == (void *)-1) {
        perror("shmat failed in main");
        exit(EXIT_FAILURE);
    }
    //-------------------初期化---------------------------------
    pthread_mutex_init(&mutex,NULL);
    pthread_cond_init(&cond_consumer,NULL);
    pthread_cond_init(&cond_producer,NULL);
    //-----------------違う点！！-------------------------------
    memset(shared_mem, 0, sizeof(SharedMemory));                //共享メモリを初期化
    shmdt(shared_mem);                                          //映射マッピングを解除し、子スレッドが再度マッピングできるようにする。

    //-----------------タイマー----------------------------------
    time_t start_time = time(NULL);
    int run_duration = 20;                                      //実行時間を設定
    
    //---------------スレッドを作る------------------------------
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