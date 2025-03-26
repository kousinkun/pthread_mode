#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <string.h>


#define BUffER_size 10                                     //共有データ数量

typedef struct
{
    int temperature;
    int humidity;
    int light;
} SensorData;

//共有リソース

struct msgbuf                                               //共享mess buffer
{
    long mtype;
    char messdata[256]; // 消息正文
};
//---------------------------------------
int msgid;                                                  //共有mess key
key_t key = 1234;
SensorData buffer[BUffER_size];                             //スレッド共有バッファ
int count = 0;                                              //バッファデータ数量を計数
//----------------------------------------
pthread_mutex_t mutex;
pthread_cond_t cond_producer, cond_consumer;

// 温度コントロー
void temperature(int temperature);
// 湿度コントロー
void humidity(int humidity);
// 光照コントロー
void light(int light);

//プロデューサースレッド：メッセージキューにデータを取る、自身の共有バッファにアップロード
void *producer(void *arg)
{

    struct msgbuf msg;                                      //msgbuf構造体を取得し、メッセージキューのバッファを作成する。
    SensorData data;                                        //共有バッファデータ

    pthread_testcancel();                                   // ✅ 允许 pthread_cancel 生效
    while (1)
    {
        pthread_mutex_lock(&mutex);                         //排他ロック

        while (count == BUffER_size)                        //bufferをいっぱいの時に、止まる、スレッドCの呼び出すを待っている。
        {
            pthread_cond_wait(&cond_producer, &mutex);
        };

        //---------メッセージキューにデータを取る、スレッドbuffにアップ-------

        if (msgrcv(msgid, &msg, sizeof(msg.messdata), 1, IPC_NOWAIT) == -1)
        { //メッセージキューにデータがない場合、取得せず即座に戻り、待機しない。

            printf("メッセージキューにデータがない，待ってください...\n");

            sleep(1);
        }
        else
        {
            //スレッドのバッファに渡す。
            memcpy(&data, msg.messdata, sizeof(SensorData));
            buffer[count++] = data;
        //-------------------------表示----------------------------

            printf("メッセージキューにデータ収集:温度=%d c, 湿度= %d %%，光照= %d  Lux\n",
            data.temperature, data.humidity, data.light);
        };

        //------------------------------------------------------------
        pthread_cond_signal(&cond_consumer);                         //データをbufferにアップロードしたら、スレッドｃを呼び出す。
        pthread_mutex_unlock(&mutex);                                //排他アンロック

        sleep(1);
    }
    return NULL;
}
//コンシューマースレッド：自身の共有バッファにデータを取る、出力する（設備調整IOTシミュレーションする）
void *consumer(void *arg)
{
    pthread_testcancel();                                           // ✅ 允许 pthread_cancel 生效
    while (1)
    {
        pthread_mutex_lock(&mutex);                                 //排他ロック
        while (count == 0)                                          //bufferが空っぽの時に、止まるスレッドPの呼び出すを待っている。
        {
            pthread_cond_wait(&cond_consumer, &mutex);
        }
        SensorData data = buffer[--count];


        //---------------------------------設備調整IOTシミュレーションする--------------------------------------
        temperature(data.temperature);
        humidity(data.humidity);
        light(data.light);
        //--------------------------------------------------------------------------------------

        pthread_cond_signal(&cond_producer);                          //データをbufferにアップロードしたら、スレッドPを呼び出す。
        pthread_mutex_unlock(&mutex);                                //排他アンロック
        sleep(1);
    }
    return NULL;
}
int main()
{
    // 获取
    pthread_t producer_thread, consumer_thread;
    key_t key = ftok("/tmp", 65);                                //キーを作成して、メッセージキューを生成する。
    if (key == -1) {
        perror("ftok failed");
        exit(1);
    }
    // 初始化
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond_consumer, NULL);
    pthread_cond_init(&cond_producer, NULL);

    //------------------messを作る----------------------
    sleep(1);    
    msgid = msgget(key,0666);                                                  //2つのプロセスを同時に実行する場合、まず1秒待たないと、メッセージキューが見つからずに停止してしまう。
    if (msgid == -1)
    {
        perror("メッセージキューがない、先に作ってください");
        return 1;
    }

    //-----------------タイマー-------------------------
    time_t start_time = time(NULL);
    int run_duration = 24;                                      //実行時間を設定

    pthread_create(&producer_thread, NULL, producer, NULL);
    pthread_create(&consumer_thread, NULL, consumer, NULL);
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
    printf("cance 1 \n");
    pthread_cancel(consumer_thread);
    printf("cance 2 \n");
    //スレッド回収する
    pthread_join(producer_thread, NULL);
    pthread_join(consumer_thread, NULL);
    //ロックと条件ロックを削除
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond_producer);
    pthread_cond_destroy(&cond_consumer);
    //メッセージキューを削除する
    if (msgctl(msgid, IPC_RMID, NULL) == 0)
    {
        printf("メッセージキューを削除した！ \n");
    }
    else
    {
        perror("msgctl");
    };

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