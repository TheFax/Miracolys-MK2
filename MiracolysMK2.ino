/*
*  SMART LEAD BATTERY TESTER aka Miracolys2
*  FAX - Aprile 2020
*  Arduino NANO 328
*  Seriale 115200
*/

#include <LiquidCrystal.h>

//Definizioni che riguardano l'hardware del dispositivo
#define PIN_V_READING_FLOAT   A0       //Il pin che esegue la lettura VBatt. (1024 = 15V)
#define PIN_V_READING_LOAD    A1       //Il pin che esegue la lettura VBatt. (1024 = 15V)
#define PIN_ENABLE_LOAD       7        //A livello logico alto, questo pin attiva il carico
#define PIN_BUZZER            9        //Buzzer passivo (dal pin esce un'onda quadra, non un livello logico)
#define LOAD_CURRENT          9        //La corrente del carico interno
#define DEAD_TIME_ON_TO_OFF   1000      //Tempo in microsecondi tra il comando logico inviato dall'arduino all'effettiva reazione dell'IGBT del carico che si apre

//Le seguenti due definizioni servono per costruire la retta y=mx+q relativa al calcolo del SoC
#define V_TOP                 13.1     //Corrisponde alla tensione a carico dopo TEMPO_TEST quando SoC è 100%
#define V_BOT                 9        //Corrisponde alla tensione a carico dopo TEMPO_TEST quando SoC è 0%

//Preferenze sui test da eseguire
#define TEMPO_TEST                7000   //Durata del DEEP TEST misurata in millisecondi
#define TEMPO_TEST_FAST           1000   //Durata del TEST FAST
#define DELTA_LETTURA_INSTABILE   5      //Misurata in raw adc
#define VOLTAGE_MAXIMUM_V_INPUT   14.5   //Al di sopra di questo valore siamo in allarme overvoltage
#define VOLTAGE_MINIMUM_V_FLOAT   10.5   //Al di sotto di questa misura (a vuoto) si considera la batteria scarica e quindi non in grado di sostenere un test
#define VOLTAGE_MINIMUM_V_LOAD    8      
#define VOLTAGE_NO_BATTERY        0.5    //Al di sotto di questa tensione, considero la batteria scollegata

//Definizioni ad uso interno
#define Q                     V_BOT
#define M                     ((V_TOP - V_BOT) / 100)

boolean is_load;

//Inizializzazione libreria LiquidCrystal
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

//Definizioni ad uso LCD
#define RIGA_1    0
#define RIGA_2    1
#define RIGA_3    2
#define RIGA_4    3

//La seguente funzione serve per resettare il uP e farlo ripartire da zero.
void( * resetFunc)(void) = 0;

//######################################################################################################################
// SETUP
//######################################################################################################################
void setup() {
   //Setup seriale
   Serial.begin(115200);
   Serial.println();
   Serial.println();
   Serial.println("SMART BATTERY TESTER - by FAX - Aprile 2020");

   //Set up LCD
   lcd.begin(16, 4);

   analogReference( INTERNAL );

   //Set up pin
   pinMode(PIN_ENABLE_LOAD, OUTPUT);
   pinMode(PIN_BUZZER, OUTPUT);

   //Pin in default state
   digitalWrite(PIN_ENABLE_LOAD, LOW);
   digitalWrite(PIN_BUZZER, LOW);
   
   disable_load();
}

void loop() {
   float tensione_vuoto;
   float tensione_carico;
   float tensione_attuale;
   long inizio_test;
   float vta, vtb;
   float SoC;
   float resistenza_serie;

   //Scritte di default sull'LCD
   lcd.clear();
   lcd.setCursor(0, RIGA_1);
   lcd.print("MIRACOLYS MK II");
   lcd.setCursor(0, RIGA_4);

   Serial.println("Send a char via serial port to show menu'");
   
   //##############################################################################################
   // PRETEST
   //##############################################################################################
   
   //Eseguo la lettura senza carico
   Serial.println("Reading voltage without load...");
   
   do {
      tensione_vuoto = voltage_reading_stabile(PIN_V_READING_FLOAT, 500); 

      if (Serial.available() > 0) {
         Serial.read();
         calibrazione();
      }
   
   } while (tensione_vuoto < VOLTAGE_NO_BATTERY);

   //##############################################################################################
   // PRIMO TEST A VUOTO
   //##############################################################################################
   
   lcd.setCursor(0, RIGA_2);
   lcd.print("Starting test");
   
   tensione_vuoto = voltage_reading_stabile(PIN_V_READING_FLOAT, 50);

   //Scrivo la lettura a vuoto sull'LCD
   lcd.setCursor(0, RIGA_3);
   lcd.print("V_float:");
   lcd.setCursor(9, RIGA_3);
   lcd.print(tensione_vuoto);
   lcd.print(" V");

   //Scrivo la tensione a vuoto via seriale
   Serial.print("Voltage without load: ");
   Serial.println(tensione_vuoto);

   //Il prossimo IF controllerà che il Miracolys sia collegato ad UNA SOLA batteria
   if (tensione_vuoto > 14.8) {
      //Arrivo qui se abbiamo collegato il Miracolys ad una fonte di alimentazione con tensione troppo alta 
      lcd.setCursor(0, RIGA_2);
      //lcd.print("                ");
      lcd.print("OVERVOLTAGE!    ");
      lcd.setCursor(0, RIGA_4);
      lcd.print("DISCONNECT NOW!");
      
      Serial.println("Blocked for input overvoltage.");
      melodia_allarme();
      wait_disconnection();
   }

   //Il prossimo IF controllerà che la batteria sia almeno un pochino carica, per poter sostenere un test
   if (tensione_vuoto < 11.0) {
      //BATTERIA SCARICA
      lcd.setCursor(0, RIGA_2);
      //lcd.print("                ");
      lcd.print("Battery LOW!    ");
      lcd.setCursor(0, RIGA_4);
      lcd.print("Recharge battery");
      
      Serial.println("Blocked because the battery voltage is too low.");
      melodia_fail();
      delay(800);
      wait_disconnection();
   }

   //##############################################################################################
   // PRIMO TEST A CARICO (TEST FAST)
   //##############################################################################################

   //Scrivo la lettura via seriale
   Serial.println("Starting measurament with load...");

   //Attivo il carico
   enable_load();
   
   //Verifico immediatamente se la batteria regge
   if (voltage_reading_puntuale(PIN_V_READING_LOAD) < 8) {
	   Serial.println(voltage_reading_puntuale(PIN_V_READING_LOAD));
      //BATTERIA NON REGGE IL CARICO
      
      //Spengo immediatamente il carico
      disable_load();

      lcd.setCursor(0, RIGA_2);
      lcd.print("Battery FAIL!");
      
      Serial.println("Blocked because the battery doesn't sustain the load.");
      melodia_fail();
      wait_disconnection();
   }

   inizio_test = millis();
   do {
      //Ora rilevo la tensione a carico
      tensione_attuale = voltage_reading_mediata(PIN_V_READING_LOAD, 100);
      //Scrivo la tensione a carico sul display
      lcd.setCursor(0, RIGA_4);
      lcd.print("V_load: ");
      lcd.setCursor(9, RIGA_4);
      lcd.print(tensione_attuale);
      lcd.print(" V  ");

      if (tensione_attuale < VOLTAGE_NO_BATTERY) {
         resetFunc();
      }
       
      if (tensione_attuale < 8) {
         disable_load();
         melodia_fail();
         lcd.setCursor(0, RIGA_2);
         lcd.print("Test aborted.");
         wait_disconnection();
      }
   } while (millis() < inizio_test + TEMPO_TEST_FAST);

   //##############################################################################################
   // SECONDO TEST A CARICO (DEEP TEST)
   //##############################################################################################
   
   //Verfico se la batteria è buona in modo superficiale
   if (tensione_attuale > (tensione_vuoto - 2)) {
      melodia_ok();
   }

   //Scrivo la tensione a carico via seriale
   Serial.print("Voltage with load: ");
   Serial.println(tensione_attuale);

   lcd.setCursor(0, RIGA_2);
   //lcd.print("                ");
   lcd.print("Testing...      ");
      
   inizio_test = millis();
   do {
      tensione_attuale = voltage_reading_stabile(PIN_V_READING_LOAD, 100);
      
      if (tensione_attuale < VOLTAGE_NO_BATTERY) {
         resetFunc();
      }
       
      if (tensione_attuale < 8) {
         disable_load();
         melodia_fail();
         //               "1234567890123456"
         lcd.setCursor(0, RIGA_2);
         lcd.print("Test aborted.");
         wait_disconnection();
      }

      lcd.setCursor(0, RIGA_4);
      lcd.print("V_load: ");
      lcd.setCursor(9, RIGA_4);
      lcd.print(tensione_attuale);
      lcd.print(" V  ");

   }  while (millis() < inizio_test + TEMPO_TEST);

   //Al tempo t-1 (ta) viene letta la tensione a carico
   //Al tempo t viene staccato il carico
   //Al tempo t+1 (tb) viene letta la tensione a vuoto

   vta = voltage_reading_mediata(PIN_V_READING_LOAD, 30);
   disable_load();
   delayMicroseconds(DEAD_TIME_ON_TO_OFF);
   vtb = voltage_reading_mediata(PIN_V_READING_FLOAT, 10);
 
   resistenza_serie = (vtb-vta) / LOAD_CURRENT;
   SoC = ( vta - Q ) / M;
   tensione_carico = vta;

   //##############################################################################################
   // RIASSUNTO FINALE
   //##############################################################################################

   lcd.clear();

   lcd.setCursor(0, RIGA_1);
   lcd.print("SoC:");
   lcd.setCursor(9, RIGA_1);
   lcd.print(round(SoC));
   lcd.setCursor(15, RIGA_1);
   lcd.print("%");

   lcd.setCursor(0, RIGA_2);
   lcd.print("ESR:");
   lcd.setCursor(9, RIGA_2);
   lcd.print(resistenza_serie);
   //lcd.print(" ");
   lcd.setCursor(15, RIGA_2);
   lcd.print(char(0xF4));

   lcd.setCursor(0, RIGA_3);
   lcd.print("V_float:");
   lcd.setCursor(9, RIGA_3);
   lcd.print(tensione_vuoto);
   lcd.print(" V");
   
   lcd.setCursor(0, RIGA_4);
   lcd.print("V_load: ");
   lcd.setCursor(9, RIGA_4);
   lcd.print(tensione_carico);
   lcd.print(" V");
   
   melodia_ok2();

   wait_disconnection();
}

byte check_voltage(float voltage){
   if (voltage > VOLTAGE_MAXIMUM_V_INPUT) {
	  //Sovratensione in ingresso
      disable_load();
   }
   
   if (voltage < VOLTAGE_NO_BATTERY) {
      //
	  resetFunc();
   }

   if (is_load == true) {
	   
	   //TUTTI I TEST A CARICO
	   
	   if (voltage < VOLTAGE_MINIMUM_V_LOAD) {
		  //Batteria non sostiene il test in corso
		  //Miracolys in pericolo di sostentamento
		  disable_load();
	   }
	   
   } else {
	   
	   //TUTTI I TEST A VUOTO
	   
	   if (voltage < VOLTAGE_MINIMUM_V_FLOAT) {
		  //Batteria non carica a sufficienza per poter iniziare un test
	   }
	   
   }
}

void enable_load() {
    //Questa funzione attiva il carico
	is_load = true;
    digitalWrite(PIN_ENABLE_LOAD, HIGH);
}

void disable_load() {
	//Questa funzione disattiva il carico
    digitalWrite(PIN_ENABLE_LOAD, LOW);
	is_load = false;
}

float voltage_reading_stabile(int pin, int stabilita) {
   /*Questa funzione esegue la lettura VBATT e si occupa anche di filtrare eventuali
    *letture instabili.
    */
   int adc_val = 0;
   int adc_min = 0;
   int adc_max = 0;
   long adc_sum = 0;

   do {
      adc_min = 1024;
      adc_max = 0;
      adc_sum = 0;
      //Eseguo "quantity" letture
      for (int i = 0; i < stabilita; i++) {
         adc_val = analogRead(pin);
         delayMicroseconds(1000);  // 1 ms per ogni misura
         adc_min = min(adc_val, adc_min);
         adc_max = max(adc_val, adc_max);
         adc_sum = adc_val + adc_sum;
      }
      //In questo punto del programma avro':
      //adc_max = Il valore più alto campionato dall'ADC
      //adc_min = Il valore più basso campionato dall'ADC
      //adc_sum = La somma di tutte le letture ADC
   } while (adc_max - adc_min > DELTA_LETTURA_INSTABILE);

   return raw_to_volt(adc_sum / stabilita);
}

float voltage_reading_mediata(int pin, int medie) {
   long adc_sum = 0;

   //Eseguo "quantity" letture
   for (int i = 0; i < medie; i++) {
      adc_sum = adc_sum + analogRead(pin);
      delayMicroseconds(100);  //0.1 ms per ogni misura
   }
   //In questo punto del programma avro':
   //adc_sum = La somma di tutte le letture ADC

   return raw_to_volt(adc_sum / medie);
}

float voltage_reading_puntuale(int pin) {
   return raw_to_volt(analogRead(pin));
}

float raw_to_volt(float raw) {
   return float((float(raw) * 15) / 1024);
}

void melodia_ok(void) {
   tone(PIN_BUZZER, 2000);
   delay(50);
   noTone(PIN_BUZZER);
   delay(10);
   tone(PIN_BUZZER, 2600);
   delay(50);
   noTone(PIN_BUZZER);
   delay(10);
   tone(PIN_BUZZER, 3200);
   delay(50);
   noTone(PIN_BUZZER);
}

void melodia_ok2(void) {
   tone(PIN_BUZZER, 2600);
   delay(50);
   noTone(PIN_BUZZER);
   delay(10);
   tone(PIN_BUZZER, 3200);
   delay(50);
   noTone(PIN_BUZZER);
   delay(10);
   tone(PIN_BUZZER, 4000);
   delay(50);
   noTone(PIN_BUZZER);
}

void melodia_allarme(void) {
   while (true) {
      tone(PIN_BUZZER, 3000);
      delay(50);
      noTone(PIN_BUZZER);
      delay(50);
   }
}

void melodia_fail(void) {
   tone(PIN_BUZZER, 1200);
   delay(100);
   noTone(PIN_BUZZER);
   delay(20);
   tone(PIN_BUZZER, 1000);
   delay(100);
   noTone(PIN_BUZZER);
   delay(20);
   tone(PIN_BUZZER, 800);
   delay(500);
   noTone(PIN_BUZZER);
}

void calibrazione() {
   lcd.clear();
   lcd.setCursor(0, RIGA_1);
   lcd.print("MIRACOLYS MK II");
   lcd.setCursor(0, RIGA_2);
   lcd.print("CALIBRATION MODE");

   while (true) {

      //Disattivo il carico
	  disable_load();
	  lcd.setCursor(0, RIGA_3);
      lcd.print("CALIBRATION MODE");
      while (true) {
         delay(70);
         lcd.setCursor(0, RIGA_4);
         lcd.print("V_float: ");
         lcd.print(voltage_reading_stabile(PIN_V_READING_FLOAT,50));
         lcd.print("  ");

         if (Serial.available() > 0) {
            Serial.read();
            break;
         }
      }
      //Attivo il carico
      enable_load();
      while (true) {
         delay(70);
         lcd.setCursor(0, RIGA_4);
         lcd.print("V_load: ");
         lcd.print(voltage_reading_stabile(PIN_V_READING_LOAD,50));
         lcd.print("   ");

         if (Serial.available() > 0) {
            Serial.read();
            break;
         }
      }
      resetFunc();
   }
}

void wait_disconnection() {
   do {
   } while (voltage_reading_puntuale(PIN_V_READING_FLOAT) > VOLTAGE_NO_BATTERY);
   resetFunc();
}
