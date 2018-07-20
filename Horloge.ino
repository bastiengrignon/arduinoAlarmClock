#include "driver_isr.h"

//--------------------A faire plus tard
/*  Version 1.1 : 
    Ajout switch pour allumer/éteindre le buzzer          (Fait logiciellement)
    Version 1.2 : 
    Ajout bouton poussoir affichage alarme                (~~~plus qu'a installer)
    Version 1.3 : 
    Utilisation d'interruption                            (fait)
    Envoi horaire des bus en fonction du réveil          (xxxEnvoi mais bizarre !)
    plusieurs alarmes                                     (~x~fait)
   
    Aucun delay() pour buzzer                             (A faire)
    
    if (jour de changement d'heure d'été){
     heure = heure + 1
    if (jour de changement d'heure d'hiver){
     heure = heure - 1
*/
//--------------------
/*Bluetooth non utilisé, utilisation voie série 
#include <SoftwareSerial.h>
SoftwareSerial HC06(10, 11); // RX | TX
*/

/************************************************************************/
/*                Librairie RTC, Afficheur 7 segments                   */
/************************************************************************/
#include <Wire.h>
#include <RTClib.h>
#include <TM1637Display.h>
//#include "driver_isr.h"

/************************************************************************/
/*                        Pins Configuration                            */
/************************************************************************/
const uint8_t CLK = 5;          //A changer en fonction des cartes utilisées
const uint8_t DIO = 6;          //A changer en fonction des cartes utilisées
const uint8_t led = 7;          //Pin voyant
const uint8_t buzzer = 11;      //Pin alarme

/************************************************************************/
/*                          Constructeurs                               */
/************************************************************************/
TM1637Display display(CLK, DIO);
RTC_DS1307 rtc;

/************************************************************************/
/*                        Variables globales                            */
/************************************************************************/
uint8_t h,m,s,a,mo,j,dw;           //Variable RTC
typedef enum{
	uint8_t h;		// heure 
	uint8_t m;		// minute
	uint8_t s;		// seconde
	uint8_t a;		// année
	uint8_t mo;		// mois
	uint8_t j;		// jours du mois
	uint8_t dw;		// jours de la semaine
}rtc_datetime;
rtc_datetime datetime;
//Configuration initiale alarme
uint8_t h_alarme = 0;
uint8_t m_alarme = 0;
uint8_t s_alarme = 0;
uint8_t temps_alarme_IT = 8;				// Durée de l'alarme (en secondes)
volatile bool isAlarm = false ;             // Alarme
volatile bool activeAlarm = false;			// Alarme activée
volatile bool FlagPoint = true ;			// Flag d'interruption 2 point (":")
// Tableau d'affichage des messages : "done", "Err_", "ACtI", "DESA"
byte done[]{
  0b01011110,
  0b00111111,
  0b01010100,
  0b01111001
};
byte error[]{
  0b01111001,
  0b01010000,
  0b01010000,
  0b00001000
};
byte acti[]{
  0b01110111,
  0b00111001,
  0b01111000,
  0b00110000,
};
byte desa[]{
  0b01011110,
  0b01111001,
  0b01101101,
  0b01110111,
};
String tabMois[13] = {"Jan ", "Fev ", "Mars", "Avr ", "Mai ", "Juin", "Juil", "Aout", "Sep ", "Oct ", "Nov ", "Dec "};
/*----  TEST  ----*/
unsigned long lastPeriodStart = 0;
const int onDuration=300;
const int periodDuration=500;
/*---- FIN TEST ----*/

/************************************************************************/
/*                       Initialisation Timer2                          */
/************************************************************************/
void initTimer2() {
  TCCR2A = 0;                                       //default
  TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20); //clk/1024 est incrémenté toutes les 64us
  TIMSK2 = (1 << TOIE2);                            // TOIE2
}

/************************************************************************/
/*                       Initialisation Timer1                          */
/************************************************************************/
void initTimer1() {
  TCCR1A = 0;				// Registre TCCR1A à 0
  TCCR1B = 0;				// Pareil pour TCCR1B
  TCNT1  = 0;				// Initialiser compteur à 0
  // set compare match register for 1hz increments
  OCR1A = 15624;			// = (16*10^6) / (1*1024) - 1 (must be <65536)
  // turn on CTC mode
  TCCR1B |= (1 << WGM12);
  // Set CS10 and CS12 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);  
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);
}

/************************************************************************/
/*                               Setup                                  */
/************************************************************************/
void setup() {
  Serial.begin(9600);
  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);              // Éteindre la LED rouge (L)
  pinMode(led, OUTPUT);               // Voyant bleu
  //pinMode(buzzer, OUTPUT);          // Alarme Buzzer
  display.setBrightness(0x09);        // Luminosité afficheur (min 0x08 / max 0x0C)
  rtc.begin();                        // Démarrer RTC
  //rtc.adjust(DateTime(2018, 04, 03, 21, 2, 00));
  //Année / Mois / Jour / Heures / Minutes / Secondes
  noInterrupts();
  initTimer2();
  initTimer1();
  interrupts();
}
/************************************************************************/
/*                               Loop                                   */
/************************************************************************/
void loop() {
  //Récupération données RTC
  DateTime now = rtc.now();
  datetime.h = now.hour();
  datetime.m = now.minute();
  datetime.s = now.second();
  datetime.a = now.year();
  datetime.mo = now.month();
  datetime.j = now.day();
  datetime.dw = now.dayOfTheWeek();
  AffichagePoint();
  //schéma de variable du display (variable à afficher, leading zéro, longueur, position)
  display.showNumberDec(h, false, 2, 0);    //Affichage heures sur les 2 permiers digits
  display.showNumberDec(m, true, 2, 2);     //Affichage minutes sur les 2 derniers digits
  // Condition de l'alarme
  digitalWrite(led, LOW);
  if (datetime.h == h_alarme && datetime.m == m_alarme && datetime.s == s_alarme){
    if (isAlarm){
      activeAlarm = true;		//Activation compteur (... seconde)
    }
  }
  Bluetooth();
}

/************************************************************************/
/*        Affichage des 2 points toutes les secondes (Timer2)           */
/************************************************************************/
void AffichagePoint() {
  if (FlagPoint) {
    display.setColon(true);
  } else {
    display.setColon(false);
  }
}

/************************************************************************/
/*                Gestion du bluetooth pour l'alarme                    */
/************************************************************************/
void Bluetooth() {
  String majA, majMo, majJ, majH, majM;
  String mode = "";
  //Variables de l'alarme
  String heure = "";
  String minutes = "";
  if (Serial.available()) {   //Si Bluetooth Connecté
    while (Serial.available() > 0) {
      mode = Serial.readStringUntil('m');
      //Serial.println((String)"Mode : " + mode);
      switch(mode.toInt()){
        /************************************************************************/
        /*                      Mode activation alarme                          */
        /************************************************************************/
        case 1:
          isAlarm = true ;    //Activation de l'alarme
          //Serial.println("Tu entre en mode activation de l'alarme");
          heure = Serial.readStringUntil(':');    //Lecture caractère recu jusqu'au ":"
          minutes = Serial.readStringUntil('\n'); //Lecture jusqu'au "\n"
          h_alarme = heure.toInt();               //Conversion et attribution de la variable recue (heure)
          m_alarme = minutes.toInt();             //Conversion et attribution de la variable recue (minutes)
          //Serial.println((String)H_ALARME + ":" + M_ALARME);
          for(int i=0;i<70;i++){
            digitalWrite(led, HIGH);  //Alerte visuelle de confirmation
            display.setSegments(acti, 4, 0);  //Affichage message done
          }
        break;
        /************************************************************************/
        /*                    Mode mise à jour de l'heure                       */
        /************************************************************************/
        case 2:
          //Serial.println("Tu entre en mise à jour de l'heure");
          majJ = Serial.readStringUntil('/');
          majMo = Serial.readStringUntil('/');
          majA = Serial.readStringUntil('#');
          majH = Serial.readStringUntil(':');
          majM = Serial.readStringUntil('\n');
          rtc.adjust(DateTime(majA.toInt(), majMo.toInt(), majJ.toInt(), majH.toInt(), majM.toInt(), 0));
          //Serial.println((String)"Nous sommes le " +majJ + " " + tabMois[majMo.toInt()] + " " + majA + "\nIl est " + majH + ":" + majM );
          for(int i=0;i<70;i++){
            digitalWrite(led, HIGH);  //Alerte visuelle de confirmation
            display.setSegments(done, 4, 0);  //Affichage message done
          }
        break;
        /************************************************************************/
        /*                   Mode désactivation de l'alarme                     */
        /************************************************************************/
        case 3:
          isAlarm = false;
          for(int i=0;i<80;i++){
            display.setSegments(desa, 4, 0);  //Affichage message done
            digitalWrite(led, HIGH);  //Alerte visuelle de confirmation
          }
        break;
		/************************************************************************/
		/*                          Mode paramétrage                            */
		/************************************************************************/
		case 4:
		
		break;
        /************************************************************************/
        /*                           Mode Defaut                                */
        /************************************************************************/
        default:
        break;
      }
      heure = "";     //Remise à zéro des valeurs de réception
      minutes = "";   //Remise à zéro des valeurs de réception
    }
    digitalWrite(led, LOW);   //Désactivation de la LED 
  }
}

/************************************************************************/
/*             Timer2, en mode débordement, toutes les 8ms              */
/************************************************************************/
ISR (TIMER2_OVF_vect) {
  static byte countLCD = 0;   //Compteur pour 2 points LCD
  static byte countLED = 0;
  // 256- 131 --> 125*64us = 8mS
  // 256- 99  --> 157*64us = 10ms
  TCNT2 = 131;
  countLCD++;countLED++;
  //=========================
  if (countLCD == 125) {            //1 secondes
    FlagPoint = !FlagPoint;         //Inversion de la variable des 2points
    countLCD = 0;
  }
  //=========================
  if(countLED == 25){               //0.2s -> 200ms
    digitalWrite(led, LOW);
    noTone(buzzer);
    if(isAlarm && activeAlarm){
      digitalWrite(led, !digitalRead(led));
      tone(buzzer, 440, 500);
      countLED = 0;
    }
  }
}

/************************************************************************/
/*              Interruption pour le temps de l'alarme                  */
/************************************************************************/
ISR(TIMER1_COMPA_vect){
  //Toute les secondes on passe ici...
  static uint8_t counter = 0;
  if(isAlarm && activeAlarm){
    //Serial.println((String)"Secondes : " + counter);
    if(counter++ == temps_alarme_IT){
      counter = 0;
      isAlarm = false;				//
      activeAlarm = false;			//désactivation compteur
      digitalWrite(led, LOW);
      //Serial.println("RAZ du compteur\nDésactivation du compteur");
    }    
  }
}
