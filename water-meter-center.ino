#include "stdio.h"

/* 
 * 注意: コードを実行する前に、debugModeを確認してください。
 * 
 * - パルス
 *   水道メータから、1Lあたりに送られる
 *   
 * - パルスの間隔
 *   パルスの間隔はpulseIntervalをmsで指定します。
 *   この間隔より短い場合はノイズとして、処理は何も行いません。
 *   コードの※1を参照
 * 
 * - パルスのカウント
 *   パルスを割り込みでカウントします。
 *   コードの※2を参照
 *   
 * - パルスの割り込み  
 *   attachInterruptで割り込みの処理を指定指定しています。
 *   attachInterruptについて、詳しい説明は下記を参照。
 *   http://www.lapis-semi.com/lazurite-jp/contents/reference/attachInterrupt.html
 *   コードの※3を参照
 *   
 * - sleepの間隔  
 *   sleepの間隔をsleepIntervalで指定します(ms)
 * 
 * - カウンタを送信する間隔
 *   カウンタの値を外部に送信してから、次に送信するまでの時間（sendInterval）をmsで指定します。
 *   1hは3600000msなので、1時間間隔を開ける場合は3600000を指定
 *   コードの※4を参照
 *   
 * - カウンター値の送信
 *   throwData()関数の中に、カウンター値の送信処理を記述してください。
 *   コードの※5を参照
 *   
 * - カウンター値送信中のパルスについて
 *   カウンター値は、送信された直後にリセットされます。
 *   そのため、送信が始まってから終わるまでに送られたパルスは、次のカウントに含まれます。
 *   
 *   ---------------------
 *     pulse
 *     pulse
 *     【1度目の送信開始】
 *     pulse <- 送信が開始した後のパルスは、2度目の送信に持ち越し
 *     【1度目の送信終了】
 *     pulse
 *     pulse
 *     pulse
 *     【2度目の送信開始】
 *     【2度目の送信終了】
 *   ---------------------
 *   上記の場合、1度目の送信でカウンタ値は2
 *   2度目の送信でカウンタ値は4となります。
 *   
 * - sleepの処理
 *   arduinoではsleep関数がないので、delayを使用しています。
 *   Lazuriteのsleepを使う場合は、delayをsleepに書き換えてください。
 *   コードの※6を参照
 *   
 *   なお、sleepの間隔は60分にしたいが、sleep(3600000)とするとハードの問題でうまく動かない。
 *   このため、sleepの間隔は短めにして（3分）
 *   sleepが20回目の時に（1時間経った時）カウントを送信するというやり方に変更しています。
 *   
 * - 出力データのカウント値の桁数について  
 *   出力時に、00001のように0で桁数を揃えたい場合、
 *   throwData()関数内の処理を変更してください。
 *   
 *   現状は下記のようになっている
 *   char strCount[5];
 *   sprintf(strCount, "%05d", pulseCount); 
 *   
 *   4桁にしたい場合は、下記のように変更
 *   char strCount[4];
 *   sprintf(strCount, "%04d", pulseCount); 
 *  
 */

// デバッグモード

boolean debugMode = false;  

// パルスのカウンタ
// sleepTimeの間隔でカウンタが0にリセットされる

int pulseCount = 0;              

// 2ピンから入力

int inputPin = 2;

// 5番ピンから出力

int resetPin = 5;

// 6ピンから出力　Wi-SUNモジュールをsleepから復帰させるのに、WakeUpする

int wakeupPin = 6;

// ※4: sleepの間隔(ms)

int sleepInterval = 30000;

// sleepを繰り返す回数
// 1時間毎に値を送信したい場合、1h / sleepInterval = 3600s / 30s = 120
// となるので、120と入力する。

int sleepRepeatTime = 120;

// パルスの長さ（ms）

int pulseInterval = 1000;

// sleepした回数

int sleepCount = 0;

// 最新の割り込みからの経過時間を保存するための変数

unsigned long previousPulseTime = 0;

// 起動ルーチンの追加：2016/02/03 Hisaki Shimodousono

int Start_Up_Count = 0;

// afterInterrupt()の関数が生成される前にsetup()内で使用しているため、
// 事前に関数の宣言をしておく必要がある。

void afterInterrupt();

void setup() {

  // ピンモードの指定
  
  pinMode(inputPin,  INPUT);  // 2番ピン  
  setPullUpPin(3);
  setPullUpPin(4);
  pinMode(resetPin,  OUTPUT); // 5番ピン
  pinMode(wakeupPin, OUTPUT); // 6番ピン  
  setPullUpPin(7);
  setPullUpPin(8);
  setPullUpPin(9);
  setPullUpPin(10);
  setPullUpPin(11);
  setPullUpPin(12);
  setPullUpPin(13);
  
  if (debugMode) {    

    // デバッグ用で、シリアルモニタにprintlnする場合

    Serial.begin(9600); 
    
  } else {
    // resetPinでWi-SUNモジュールのリセット
    digitalWrite(resetPin, HIGH);
    delay(500);
      
    digitalWrite(resetPin, LOW);
    delay(500);
      
    digitalWrite(resetPin, HIGH);
    delay(500);

    // wakeupPinでスリープからの解除
    digitalWrite(wakeupPin, HIGH);
    delay(1000);
    sendSkCommands();
    
  }

  // ※3: 割り込み時の処理を指定。
  // 第一引数を"0"と指定することで、ピン2を外部割り込みとして使用
  // 第二引数で割り込みが発生したときにcallする関数を指定
  // 第三引数で割り込みを発生させるためのモードを指定します。
  //   指定可能なモードは次の通りです。
  //   LOW: ローレベルのときに割り込みが発生します。
  //   RISING:  信号の立ち上がりエッジで割り込みが発生します。
  //   FALLING: 信号の立下りエッジで割込みが発生します。
  //   CHANGE: 信号が変化したときに割り込みが発生します。
  
  attachInterrupt(0, afterInterrupt, RISING);
  
  // 起動ルーチンの追加：2016/02/03 Hisaki Shimodousono

  Start_Up_Count = 0;
}

void loop() {
  
  // sleepから抜けた時の処理

  afterAwake();

  // ※6: Lazuriteではsleepにする。Arduinoではdelay関数を利用する。

  sleep(sleepInterval);
}

void setPullUpPin(int pin) {
  // ピンを入力に設定
  
  pinMode(pin, INPUT); 

  // プルアップ抵抗を有効に
  
  digitalWrite(pin, HIGH);
}

void sendSkCommands() {
  Serial.begin(115200); 

  //水道BOX毎にIDが異なる。10台分書き込み時に設定
    
  Serial.println("SKSREG S1 12345678abcdef08");
  delay(100);

  //Ch33(922.5MHz)を選択　//Chは全ての台数を同じにする
    
  Serial.println("SKSREG S2 21");
  delay(100);
    
  //PAN ID 0x8888を選択//PANは全ての台数を同じにする
    
  Serial.println("SKSREG S3 8888");
  delay(100);
}

void afterInterrupt() {    
    
  // オーバーフロー対策の追加：2016/02/03 Hisaki Shimodousono
  // millis()はunsigned long型のため、4,294,967,295ミリ秒(約49日)でオーバーフローするため、0に戻ります。
  // ((millis() - previousPulseTime) < pulseInterval) この条件では1000ms経過した時に割り込んで来ても、カウントを無視する可能性が有ります。
  // オーバーフローで0に戻っても正常に経過時間を見るために改造
  
  unsigned long nowTime = millis();
  unsigned long elapsed_time = 0;
    
  if (nowTime >= previousPulseTime) {
    elapsed_time = nowTime - previousPulseTime;
  } else {
    // オーバーフロー対策
    elapsed_time = 0xffffffff + nowTime - previousPulseTime + 1;
  }
    
  if (elapsed_time < pulseInterval) {   // 1000ms経過した？

    // ※1: 一定時間内に割り込みが入った場合、その割り込みは無視される
    // ここに一定時間内に割り込みが入った場合の処理
    // デバッグ用に、一定時間内の割り込み時に文字列を出力    

    // Serial.println("LESS THAN 1000ms!!!");
    
  } else {
    previousPulseTime = millis();           // 現在の経過時間

    // ※2: カウンタをインクリメント

    countUp();
  }
}

void countUp() {
  pulseCount++;
}

void afterAwake() {

  char pulseDebug[5];
  sleepCount++;
  
  // デバッグ用
  sprintf(pulseDebug, "%d", pulseCount);
  Serial.print("pulseCount is... ");
  Serial.println(pulseDebug);
  
  // 起動ルーチンの追加：2016/02/03 Hisaki Shimodousono
  // 起動直後、30sごとにカウンタの値を送信する
  // 計6回(3分間)
  if(Start_Up_Count < 6) {
    Start_Up_Count ++;    // カウントUp
  
    // カウンタの値を送信する
    throwData();    

    // カウンタをリセット
    resetCount();
  }
 
  // 起動ルーチンの追加：2016/02/03 Hisaki Shimodousono
  
  if (sleepCount >= sleepRepeatTime) {     
    // カウンタの値を送信する
  
    throwData();    

    // カウンタをリセット

    resetCount();
  }
}

void resetCount() {
  pulseCount = 0;
  sleepCount = 0;
}

void throwData() {

  // ※5: 本来であれば、ここで外部に結果を送信するが
  // ここではデバック用にシリアルモニタに出力させるようになっている。
  
  // To store 4 digits number string
  // ID2電子水道メータアダプターVer0.1.txtのサンプルのように下記の宣言をcountUp()の外で行うと、
  // カウンタをインクリメントしても出力がインクリメントされない。
  // そのためcountUp()内で下記を宣言すること。
  
  char strCount[5];

  // 出力結果の数値に、0のパディングを入れたいのでsprintfで結果を整形

  sprintf(strCount, "%05d", pulseCount); 
  
  digitalWrite(wakeupPin, HIGH);
  delay(1000);
    
  digitalWrite(wakeupPin, LOW);
  delay(1000);
  
  // カウント値を送信
  
  Serial.print("SKSENDTO 1 FE80:0000:0000:0000:1034:5678:ABCD:EF01 0E1A 0 0005 ");
  Serial.print(strCount);
  Serial.println(" ");
    
  digitalWrite(wakeupPin, HIGH);
  delay(1000);
  
  //カウント値を送信後sleepする
  
  Serial.println("SKDSLEEP");
}
