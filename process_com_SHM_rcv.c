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
    int write_index;                  //　書き込みインデックス
    int read_index;                   // 読み込みインデックス
} SharedMemory;

SharedMemory *shared_mem = NULL;
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
// 温度コントロー
void temperature(int temperature);
// 湿度コントロー
void humidity(int humidity);
// 光照コントロー
void light(int light);

//プロデューサースレッド：共有メモリにセンサーデータを収集する
void *producer(void *arg)
{
    
    SensorData data;                                                 //スレッドのバッファを取得する。
    shared_mem = (SharedMemory *)shmat(shmid, NULL, 0);             //マッピング映射と解除映射が繰り返され、パフォーマンスに影響を与える。
    if (shared_mem == (void *)-1) {
        perror("shmat failed");
        pthread_exit(NULL);
    }
    
    while (1)
    {
        pthread_testcancel();                               // ✅ 允许 pthread_cancel 生效
        pthread_mutex_lock(&mutex);                          //排他ロック

        while (count == BUffER_size)                        //bufferをいっぱいの時に、止まる、スレッドCの呼び出すを待っている。
        {
            pthread_cond_wait(&cond_producer, &mutex);
        };

        //---------共有メモリにデータを取る、スレッドbuffにアップ----------
        //共有メモリ内のデータが有効かどうかを確認する。
        if (shared_mem->read_index != shared_mem->write_index)
        {
            //共有メモリからデータを読み取り、ローカルバッファに渡す。
            memcpy(&data, &shared_mem->buffer[shared_mem->read_index], sizeof(SensorData));         
             //データをローカルバッファに渡す。
            buffer[count++] = data;                                                       
            //読み取りポインタを更新する。
            shared_mem->read_index = (shared_mem->read_index + 1) % BUffER_size;

            printf("共有メモリにデータ収集:温度=%d c, 湿度= %d %%，光照= %d  \n",
                data.temperature, data.humidity, data.light);
        }
        else
        {
            //データが利用できない場合、情報を表示する。
            printf("共有メモリにデータがない，待ってください...\n");
        }
        //上传完之后要更新索引
        //------------------------------------------------------------
        pthread_cond_signal(&cond_consumer);                    //データをbufferにアップロードしたら、スレッドCを呼び出す。
        pthread_mutex_unlock(&mutex);                           //排他アンロック

        sleep(2);
    }
    
    shmdt(shared_mem);                                              // 解除映射
    return NULL;
}
//コンシューマースレッド：データを共有メモリにアップロード
void *consumer(void *arg)
{

    pthread_testcancel(); // ✅ 允许 pthread_cancel 生效
    while (1)
    {
        
        pthread_mutex_lock(&mutex);

        while (count == 0)
        {
            pthread_cond_wait(&cond_consumer, &mutex);
        }
        SensorData data = buffer[--count];
        // 更新读取指针

        // 这里开始可以做一些上传的一些操作或通信什么的
        //-------------------------------------------------------------------------------------
        temperature(data.temperature);
        humidity(data.humidity);
        light(data.light);
        //--------------------------------------------------------------------------------------

        pthread_cond_signal(&cond_producer);
        pthread_mutex_unlock(&mutex);
        sleep(2);
    }
    return NULL;
}
int main()
{
    //获取ロックID
    pthread_t producer_thread, consumer_thread;
    //制作一个钥匙来制作信息量集
    key_t key = ftok("/tmp",65);
    if (key == -1) {
        perror("ftok failed");
        exit(1);
    }

    //--------------共有メモリの識別子を作成する。------------------
    shmid = shmget(key, sizeof(SharedMemory),0666);    //10個SensorData　sizeのメモリ。
    if(shmid == -1){
        perror("共有メモリ作る失敗");
        exit(EXIT_FAILURE);
    }
    //-------------------初期化---------------------------------
    pthread_mutex_init(&mutex,NULL);
    pthread_cond_init(&cond_consumer,NULL);
    pthread_cond_init(&cond_producer,NULL);
    
    //-----------------タイマー----------------------------------

    time_t start_time = time(NULL);
    int run_duration = 24;                                      //実行時間を設定
    //---------------スレッドを作る------------------------------
    pthread_create(&producer_thread, NULL, producer, NULL);
    pthread_create(&consumer_thread, NULL, consumer, NULL);
    //-----------------------------------------------------------
    //実行時間を監視
    while (1)
    {
        time_t current_time = time(NULL);
        if (difftime(current_time, start_time) >= run_duration)
        {
            printf("プロセスも実行 %d 秒，終了します。\n", run_duration);

            break;
        }
        sleep(1);
    }
    //キャンセ信号をスレッドに送る
    pthread_cancel(producer_thread);
    pthread_cancel(consumer_thread);
    //スレッド回収する
    pthread_join(producer_thread, NULL);
    pthread_join(consumer_thread, NULL);
    //ロックと条件ロックを削除
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond_producer);
    pthread_cond_destroy(&cond_consumer);
    // 共享内存削除する
    if(shmctl(shmid, IPC_RMID, NULL)==0){
        printf("共有メモリを削除しました。\n");
    }
    return 0;
}

void temperature(int temperature)
{
    if (temperature > 30)
    {

        printf("温度 = %dc, エアコンを開く \n", temperature);
    }
    else if (temperature < 20)
    {
        printf("温度 = %dc, 暖房を開く \n", temperature);
    }
    else
    {
        printf("温度 = %dc,　\n", temperature);
    }
    return;
}
void humidity(int humidity)
{
    if (humidity > 70)
    {

        printf("湿度 = %d%%, 乾燥機を開く \n", humidity);
    }
    else if (humidity < 50)
    {
        printf("湿度 = %d%%, 加湿器を開く \n", humidity);
    }
    else
    {
        printf("湿度 = %d%%,　正常　 \n", humidity);
    }
    return;
}
void light(int light)
{
    if (light > 700)
    {

        printf("光照 = %d, 電球を消す \n", light);
    }
    else if (light < 500)
    {
        printf("光照 = %d, 電球をつける \n", light);
    }
    else
    {
        printf("光照 = %d,　正常　 \n", light);
    }
    return;
}