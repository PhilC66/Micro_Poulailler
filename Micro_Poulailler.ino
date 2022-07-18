/*	Philippe Corbel 02/2018
 *	Commande Automatique porte poulailler
 *  Carte Mini ou ProMini Arduino (faible consommation)
 *  Lipo 3.7V et panneau solaire
 *  Horloge RTC I2C DS3231 réveil Micro interval regulier (1mn)
 *	Horloge reste à l'heure d'hiver 				affichage corrigé ete/hiver
 *  Toutes les heures sont en heure Hiver,  affichage corrigé ete/hiver
 *  Relais statique TB6612
 *  Moteur 6V démultiplié
 *  LDR mesure mumiere ambiante
 *  Ecran LCD I2C attention 3V3
 *  3 BPoussoires
 *  config en 	EEPROM adr 10
 *  adresse LCD EEPROM adr 0 pas encore operationel
 
 * de 05h00 à 07h00 lux ouvre
 * de 08h00 à 17h00 ouvert
 * de 17h00 à 21h00 lux ferme
 * de 21h00 à 05h00 fermée
 
 todo
  
 IDE Arduino 1.8.16, AVR 1.8.4
 21228 69%, 755 36%
 V2-3 07/03/2022 installé Guillaume, installé Yves 18/07/2022
 correction bug timeout moteur et timeout 90s
 ajouter #ifdef gestion version Yves Mini  et Guillaume ProMini
 #ifdef __AVR__ // mini
 #ifdef ARDUINO_AVR_PRO // Pro Mini
 testé OK

 V2-2 08/01/2019, installé Yves 09/01/2019
 Heure matin H7 devient H8 08H00 ouverture obligatoire
 
 V2-1 29/04/2018, installé Yves 12/05/2018, Guillaume 27/08/2018
 Ajout fonction Manu/Auto
 
 V2.0
 installation capteur effet HALL comptage des 1/2t poulie
 monter/descente gérée en nombre de 1/2 tours sans comptage du temps
 
 probleme vitesse montée/descente differente, 
 cree un décalage important apres plusieurs cycles
 il faut creer une calibration differente M/D 
 
 V1.2 17/04/2018 
 procedure de calibration porte
 0 - Porte Fermée?   pas +-1s(10*100ms)
 1 - Ouverture pas à pas +-5s, calcul cpt0 *=50, cptf=cpto-50
 2 - Fermeture cptf, pas +-0.1S ->cptf
 3 - Ouverture cpto, pas +-0.1s ->cpto
 4 - Enregistrement EEPROM cpto,cptf, porteOuverte
 
 
 V1.2 09/04/2018, mise en conformité avec sketch J http://www.gammon.com.au/forum/?id=11497 , depuis fonctionne OK
 21378 74%, 704 34%
 
 */

#include <DS3232RTC.h>    			//http://github.com/JChristensen/DS3232RTC
#include <EEPROM.h>							// variable en EEPROM
#include <EEPROMAnything.h>			// variable en EEPROM
#include <avr/sleep.h>
#include <LiquidCrystal_I2C.h>

#define alarmInput 					2			// Alarme RTC 									Interrupt
#define IpBp0								3			// Entrée Bp0 (principal/valid) Interrupt
#define OpMesureLight				4			// Sortie Alimentation pont LDR
#define OpLEDRouge				 	5			// Sortie Led rouge Alarme
#define IpTour							6			// Entrée comptage nombre de 1/2 tour poulie
#define OpHall							7			// Sortie alimentation capteur Hall 0=AlimON
#define IpBp1								8			// Entrée Bp1					 					Pin change
#define IpBp2								9			// Entrée Bp2										Pin change
#define OpMotorEnable				10		// Sortie Commande Moteur
#define OpOuverture					11		// Sortie Commande Ouverture
#define OpFermeture					12		// Sortie Commande Fermeture
#define OpRlMotEnable				13		// Sortie Relais Moteur Enable 0=Stby

#ifdef ARDUINO_AVR_PRO
	#define Lcd_Adr							0x3F	// 63 : carte Guillaume
#elif ARDUINO_AVR_MINI
	#define Lcd_Adr							0x27	// 39 : carte Yves
#endif


String ver =  "2-3 07/03/2022";
boolean 	StartTimerAffichage = false;	// lancement timer pour time out
boolean 	timeOut = false;							// si false sortir d'un menu resté ouvert apres xx s
boolean 	flagOnceDay	= false;					// true a premiere ouverture, false X heure avant nuit, evite fermeture en cours de journée si sombre
byte 			max_menu  = 11;								// nombre de menus
byte      keep_ADCSRA;
uint32_t  timeout = 60000;							// durée time out ecran lcd en ms
uint32_t  timer2;												// timer utilisé pour timeout affichage
uint32_t  debut;												// debog seulement

struct config_t{
	int 			magic;						// numéro magic
	long 			HDebutNuit;				// Heure début de nuit
	long 			HFinNuit;					// Heure fin de nuit
	boolean 	keyLight;					// prise en compte seuil lumiere
	int				seuilJour;				// seuil lumiere nuit %
	int				N_Ouverture;			// nombre 1/2 tour ouverture porte
	int				N_Fermeture;			// nombre 1/2 tour fermeture porte
	boolean		PorteOuverte;			// position porte true = ouverte
	boolean		Auto;							// auto = 1
} config;

volatile boolean Wkup   = false;	// wakeup activé
volatile boolean Bp0	  = false;	// Bp0 pressé en phase normal
volatile long    bounce	= 0;			// timer antirebond
volatile boolean Bp00	  =	false;	// Bp0 pressé en phase menu
volatile boolean Bp1	  =	false;	// Bp1 pressé en phase menu
volatile boolean Bp2	  =	false;	// Bp2 pressé en phase menu

// caracteres acentués http://blog.bbp-online.com/arduino-lcd-et-custom-caracteres/
///////////////////////Définition caractères spéciaux pour lcd///////////////////////////
// "°" char(223)
// "|" char (124)
byte e130[8] = { B00010, B00100, B01110, B10001, B11111, B10000, B01110, B00000}; /// char(0)"é chr130"
byte e138[8] = { B01000, B00100, B01110, B10001, B11111, B10000, B01110, B00000}; /// char(1)"è chr138"
byte e256[8] = { B00000, B00100, B00100, B00100, B00100, B10101, B01110, B00100}; /// char(2)"fleche basse"
byte e257[8] = { B00100, B01110, B10101, B00100, B00100, B00100, B00100, B00000}; /// char(3)"fleche haute"
byte e258[8] = { B01110, B01110, B01110, B01110, B01110, B01110, B01110, B01110}; /// Char(4) "triple |||"
byte e259[8] = { B00000, B01100, B01100, B00000, B00000, B00000, B00000, B00000}; /// Char(5) "point haut"

// byte adr = 39;
// while (!eeprom_is_ready());
	// int longueur = EEPROM_writeAnything(0, adr);


// EEPROM_readAnything(0, adr);
// Serial.print(F("Adresse lcd")),Serial.println(adr),Serial.println(adr,HEX);


// #include <Wire.h>
// Wire.begin();
// byte error, address;
// Wire.beginTransmission(63);
    // error = Wire.endTransmission();
// if(error == 0){
	// LiquidCrystal_I2C lcd(0x3F, 16, 2);//0x27 = dec39, 0x3F = dec63
// }

// LiquidCrystal_I2C lcd2(0x27, 16, 2);

LiquidCrystal_I2C lcd (Lcd_Adr, 16, 2);//0x27 = dec39, 0x3F = dec63

//---------------------------------------------------------------------------
void WakeUp(){										//	interruption depuis RTC
	Wkup = true;	
  sleep_disable();	// cancel sleep as a precaution  
  detachInterrupt(digitalPinToInterrupt(alarmInput));
}
void RtnBp0(){										// interruption Bp0 en phase normale
	if(millis()-bounce > 30){
		sleep_disable();	// cancel sleep as a precaution		
		Bp0 = true;
		bounce = millis();
	}
}

void RtnBp00(){										// interruption Bp0 en phase menu
	if(millis()-bounce > 50){//20
		Bp00 = true;
		bounce = millis();
	}	
}

ISR (PCINT0_vect) {								// interruption Bp1 ou Bp2 en phase menu void RtnBpmenu()
 // handle pin change interrupt for D8 to D13 here
 if(millis()-bounce > 50){//20
		if(!digitalRead(IpBp1)) Bp1 = true;
		if(!digitalRead(IpBp2)) Bp2 = true;
		bounce = millis();
	}	
}

//---------------------------------------------------------------------------
void setup(){
	
	Serial.begin(9600);
	
	lcd.init(); // initialisation de l'afficheur (temps necessaire >1s)
	
	pinMode(alarmInput, INPUT_PULLUP);
	pinMode(IpBp0 , INPUT_PULLUP);
	pinMode(IpBp1 , INPUT_PULLUP);
	pinMode(IpBp2 , INPUT_PULLUP);
	pinMode(IpTour, INPUT_PULLUP);
	pinMode(OpMesureLight, OUTPUT);
	pinMode(OpMotorEnable, OUTPUT);
	pinMode(OpRlMotEnable, OUTPUT);
	pinMode(OpFermeture, OUTPUT);
	pinMode(OpOuverture, OUTPUT);
	pinMode(OpLEDRouge , OUTPUT);
	pinMode(OpHall     , OUTPUT);
	digitalWrite(OpMesureLight, LOW);
	digitalWrite(OpMotorEnable, LOW);
	digitalWrite(OpRlMotEnable, LOW);
	digitalWrite(OpFermeture  , LOW);
	digitalWrite(OpOuverture  , LOW);
	digitalWrite(OpLEDRouge   , LOW);
	digitalWrite(OpHall       , HIGH);	// Capteur OFF
	
	/* Lecture configuration en EEPROM	 */
  while (!eeprom_is_ready());
	EEPROM_readAnything(10, config);
  delay(100);//Obligatoire compatibilité avec PC Portable
	int magic = 12351;
	if (config.magic != magic) {
    /* verification numero magique si different
				erreur lecture EEPROM ou carte vierge
    		on charge les valeurs par défaut */
    //Serial.println(F("Nouvelle carte vierge !"));
    config.magic 				  = magic;
    config.HDebutNuit		  = 75600;	// 21H00 79200;	// 22h00	
    config.HFinNuit			  = 18000;	// 05H00 19800;	// 05h30
		config.keyLight			  = true;		// niveau de lumiere ferme le soir
		config.seuilJour		  = 15;
		config.N_Fermeture 		= 18;
		config.N_Ouverture 		= 18;
		config.PorteOuverte		= true;
		config.Auto						= true;
		
		EcrireEEPROM(10);
	}
	PrintEEPROM();	
	
	// initialize the alarms to known values, clear the alarm flags, clear the alarm interrupt flags
	RTC.setAlarm(ALM2_MATCH_DATE, 0, 0, 0, 1);
	RTC.alarm(ALARM_2);
	RTC.alarmInterrupt(ALARM_2, false);
		
	setSyncProvider(RTC.get);   // the function to get the time from the RTC
	if(timeStatus() != timeSet){
		Serial.println(F("Unable to sync with the RTC"));
	}
	else{
		Serial.println(F("Heure système mise à jour par RTC")); 
	}

	RTC.setAlarm(ALM1_MATCH_SECONDS, 5, 0, 0, 0);	// set Alarm 1 to occur at 5 seconds after every minute
	RTC.alarm(ALARM_1); 													// clear the alarm flag	
	RTC.squareWave(SQWAVE_NONE); 									// configure the INT/SQW pin for "interrupt" operation (disable square wave output)	
	RTC.alarmInterrupt(ALARM_1, true); 						// enable interrupt output for Alarm 1

	lcd.createChar(0, e130);
	lcd.createChar(1, e138);
	lcd.createChar(2, e256);
	lcd.createChar(3, e257);
	lcd.createChar(4, e258);
	lcd.createChar(5, e259);
		
	noInterrupts();
	attachInterrupt(digitalPinToInterrupt(alarmInput), WakeUp, LOW);// ! falling/rising pas utilisable wakeup
	attachInterrupt(digitalPinToInterrupt(IpBp0), RtnBp0, LOW);
	interrupts();

	// setTime(9, 4, 00, 7, 4, 2018);   //set the system time to 23h31m30s on 13Feb2009
	// RTC.set(now());                  //set the RTC from the system time
	if(config.Auto)GestionPorte();
}
//---------------------------------------------------------------------------
void loop(){
	if(timer2 > millis()) timer2 = millis();	
	
	if(Wkup)	ActionWakeup();

	if(Bp0) GestionMenu();
	
	Serial.flush();

	keep_ADCSRA = ADCSRA;						// enregistrement parametre ADC
	ADCSRA = 0; 										// arret convertisseur ADC
	
	set_sleep_mode (SLEEP_MODE_PWR_DOWN); //http://www.gammon.com.au/forum/?id=11497
  sleep_enable();
	noInterrupts (); 
	attachInterrupt(digitalPinToInterrupt(alarmInput), WakeUp, LOW);//	V1.2
	EIFR  = bit (INTF0);  			// clear flag for interrupt 0 					V1.2
  MCUCR = bit (BODS) | bit (BODSE);  // turn on brown-out enable select.
  MCUCR = bit (BODS);        // this must be done within 4 clock cycles of above.	
  interrupts ();             // guarantees next instruction executed
  sleep_cpu ();              // sleep within 3 clock cycles of above
	
	
	sleep_disable();
  ADCSRA = keep_ADCSRA;				// restitution parametre ADC
}
//---------------------------------------------------------------------------
void ActionWakeup(){
	
	ledcligno(2);
	if(Wkup)Wkup = false;
		
	setSyncProvider(RTC.get);   // the function to get the time from the RTC	
	
	if(config.Auto)GestionPorte();
	
	char bidon[17];
	sprintf(bidon,"%s %02d/%02d/%04d %02d:%02d","RTC :",day(),month(),year(),hour(),minute());
	Serial.print(bidon);	
	sprintf(bidon,"%s %02d/%02d/%04d %02d:%02d"," W/S :",day(),month(),year(),myHour(hour()),minute());
	Serial.println(bidon);
	
	// attachInterrupt(digitalPinToInterrupt(alarmInput), WakeUp, LOW); //V1.2
	
	if(batpct() <= 60){// Alarme Batterie, LedRouge clignotante 15s
		ledcligno(15);
	}
	
	RTC.alarm(ALARM_1);  // reset the alarm flag	doit etre imperativement derniere ligne avant fin de boucle
}
//---------------------------------------------------------------------------
void GestionPorte(){
	unsigned int H8			= 28800; // 08h00
	unsigned int H17 		= 61200; // 17H00
	long Heureactuelle 	= hour()*60;// calcul en 4 lignes sinon bug!
	Heureactuelle += minute();
	Heureactuelle  = Heureactuelle*60;
	Heureactuelle += second(); // en secondes
	
	if(Heureactuelle >= config.HDebutNuit || Heureactuelle < config.HFinNuit){ // Nuit
		if(config.PorteOuverte){ 							// si porte ouverte
			Moteur(false,config.N_Fermeture,true,false);//fermeture
		}
	}
	else if(Heureactuelle >= H8 && Heureactuelle < H17){ // Jour
		if(!config.PorteOuverte){ 						// si porte fermée
			Moteur(true,config.N_Ouverture,true,false);//ouverture
		}
	}
	else if(Heureactuelle >= config.HFinNuit && Heureactuelle < H8){ // interval auto matin
		if(config.keyLight){
			if(mesureLum() > (config.seuilJour)){
				if(!config.PorteOuverte){ 						// si porte fermée
					Moteur(true,config.N_Ouverture,true,false);//ouverture
				}
			}
		}
	}
	else if(Heureactuelle >= H17 && Heureactuelle < config.HDebutNuit){ // interval auto soir
		if(config.keyLight){
			if(mesureLum() < (config.seuilJour)){
				if(config.PorteOuverte){ 							// si porte ouverte
					Moteur(false,config.N_Fermeture,true,false);//fermeture
				}
			}
		}
	}
}
//---------------------------------------------------------------------------
int mesureLum(){
	int lum = 0;
	digitalWrite(OpMesureLight, HIGH);// Alimentation du pont LDR
	delay(100);
	lum = map(analogRead(A0),0,1023,0,100);		
	Serial.print(F("lum=")),Serial.println(lum);
	digitalWrite(OpMesureLight, LOW);
	return lum;
}
//---------------------------------------------------------------------------
void Moteur(boolean S, int nbr, boolean record, boolean anim){
	/* 	S 			sens rotation S=1 Ouverture, S=0 Fermeture
			nbr 		nombre de 1/2 tours
			record	= 1 enregistre position porte en EEPROM
			anim		= 1 effectue une animation sur le LCD		
			tmax		= timeout en cas de blocage moteur ou perte comptage
	*/
	uint32_t tmax  = 90000;	//	1.5mn
	uint32_t debut = millis();
	byte     coup  = 0;
	boolean	 last  = 1;
	
	digitalWrite(OpHall,LOW);						//	alimentation capteur Hall
	digitalWrite(OpRlMotEnable, HIGH);
	if(S){
		digitalWrite(OpFermeture	, HIGH);
		digitalWrite(OpOuverture	, LOW);
	}
	else{
	digitalWrite(OpFermeture	, LOW);
	digitalWrite(OpOuverture	, HIGH);
	}
	
	digitalWrite(OpMotorEnable, HIGH);	// alimentation moteur
	delay(1000);												// rotation minimum au demarrage pour s'ecarter de l'aimant d'arret
	
	do{		
		if(digitalRead(IpTour) == 0){			// detection passage aimant				
			if(last == 1){
				last = 0;
				coup ++;
				ledcligno(1);	
			}
		}
		else{
			last = 1;
		}
	}while((coup < nbr) && ((millis() - debut) < tmax));
		
	digitalWrite(OpMotorEnable, LOW);	// arret moteur	
	
	digitalWrite(OpFermeture	, LOW);
	digitalWrite(OpOuverture	, LOW);
	digitalWrite(OpRlMotEnable, LOW);
	
	digitalWrite(OpHall,HIGH);						//	coupure alimentation capteur Hall
	
	if(S){
		config.PorteOuverte = true;
	}
	else{
		config.PorteOuverte = false;	
	}
	
	Serial.print(F("durée = ")),Serial.print(coup),Serial.print(","),Serial.println(millis()-debut);
	if(record) EcrireEEPROM(10);//enregistrer EEPROM

}
//---------------------------------------------------------------------------
void animation(byte nbr){
	// nbr nombre de caracteres carré plein sur la ligne
	lcd.setCursor(0,1);
	for(byte j = 0;j < 16;j++){
		if(j<nbr){
			lcd.write(255);
		}
		else{
			lcd.write(32);
		}		
	}	
}
//---------------------------------------------------------------------------
void GestionMenu(){
	
	static byte menu = 0;
	
	detachInterrupt(digitalPinToInterrupt(IpBp0));
	detachInterrupt(digitalPinToInterrupt(alarmInput));
		
	StartTimerAffichage = true;
	
	// on cree de nouvelles interruptions temporaires pour les menus
	attachInterrupt(digitalPinToInterrupt(IpBp0), RtnBp00, FALLING);
	pciSetup(IpBp1);
  pciSetup(IpBp2);
	interrupts();

	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0, 0);
	lcd.print(F("Poulailler Auto"));
	lcd.setCursor(0,1);
	char bidon[17];
	sprintf(bidon,"%02d/%02d/%04d %02d:%02d",day(),month(),year(),myHour(hour()),minute());
	lcd.print(bidon);
	delay(1000);
	
	// int i=0;// a supprimer version finale
	
	do{
		switch (menu){
			case 0:
				menu = menu0(menu);// Ouvrir Next Fermer
			break;
			case 1:
				menu = menu1(menu);// Affichage Etat Batterie
			break;
			case 2:
				menu = menu2(menu);// Affichage Lux mesuré
			break;
			case 3:
				menu = menu11(menu);// // Gestion Auto/Man
			break;
			case 4:
				menu = menu3(menu);// Gestion keyLight
			break;			
			case 5:
				menu = menu4(menu);// Reglage seuil lux
			break;
			case 6:
				menu = menu5(menu);// Regler heure debut nuit
			break;
			case 7:
				menu = menu6(menu);// Regler heure fin nuit
			break;
			case 8:
				menu = menu7(menu);// Regler Date RTC
			break;
			case 9:
				menu = menu8(menu);// Regler Heure RTC
			break;
			case 10:
				menu = menu9(menu);// Calibration porte
			break;
			case 11:
				menu = menu10(menu);// About
			break;
			
			case 99:
			timeOut = true;
			break;
		}
 /*//a conserver pour voir si stuck menu// a supprimer version finale. V1.2
		if (Bp00){
			//Serial.println(F("Bp0 pressed"));			
			resetBp();			
			break;
		}
		if (Bp1){
			//Serial.println(F("Bp1 pressed"));
			resetBp();
			i++;
			//Serial.println(i);
			if(i>9)timeOut = true;
		}
		if (Bp2){
			//Serial.println(F("Bp2 pressed"));
			resetBp();
			i--;
			//Serial.println(i);
			if(i<-9)timeOut = true;
		}
 //a conserver pour voir si stuck menu// a supprimer version finale	
 */ 
		// //Serial.println(i);
		Timer_Affichage();
		if(menu == 99)timeOut = true;
	} while(!timeOut);
	
	if(timeOut){
		Serial.println(F("sortie des menus, retour mode normal"));
		delay(100); // obligatoire
		resetBp();
		Bp0 = false;
		timeOut = false;
		menu = 0;
		lcd.noDisplay();
		lcd.noBacklight();
		
		// on efface les interruptions temporaires
		// detachInterrupt(digitalPinToInterrupt(IpBp0));// 								supp V1.2
		// http://www.locoduino.org/spip.php?article70
		// bitClear(PCICR,1) ;// detach interrupt groupe PCIE1 pin 8-14 		supp V1.2
		// bitClear(PCMSK0,1);// detach pin 9																supp V1.2
		// bitClear(PCMSK0,0);// detach pin 8																supp V1.2
				
		// on reactive l'interrution mode normal		
		// attachInterrupt(digitalPinToInterrupt(IpBp0), RtnBp0, LOW);//				supp V1.2
		// attachInterrupt(digitalPinToInterrupt(alarmInput), WakeUp, LOW);// 	supp V1.2
		
		/* apres passage dans menu, les interrupt ne sont pas retabli correctement
		impossible de retrouver un fonctionnement normal
		reset soft en attendant de trouver une solution!			
		*/
		asm volatile ("  jmp 0");	//	Reset Soft
	}
}
//---------------------------------------------------------------------------
int menu0(int menu){	// Ouvrir Next Fermer

	lcd.clear();
	lcd.backlight();
	if(config.PorteOuverte){
		Porte_OF(true);// lcd Porte Ouverte
		Serial.println(F("Porte Ouverte"));
	}
	else{
		Porte_OF(false);// lcd Porte Fermée
		Serial.println(F("Porte Fermee"));
	}	
	lcd0();
	
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			if(!config.PorteOuverte){
				lcd.clear();
				lcd.setCursor(4,0);
				lcd.print(F("Ouverture"));
				Serial.println(F("Ouverture"));
				Moteur(true,config.N_Ouverture,true,false);//Ouverture
				lcd.clear();
				Porte_OF(true);// lcd Porte Ouverte
			}
			lcd0();
		}
		if (Bp2){
			resetBp();
			if(config.PorteOuverte){
				lcd.clear();
				lcd.setCursor(4,0);
				lcd.print(F("Fermeture"));
				Serial.println(F("Fermeture"));
				Moteur(false,config.N_Fermeture,true,false);//fermeture
				lcd.clear();
				Porte_OF(false);// Porte Fermée
			}
			lcd0();
		}
		Timer_Affichage();	
	}while(!timeOut);
	return menu;
}
//---------------------------------------------------------------------------
void Porte_OF(boolean Ouvert){
	lcd.setCursor(1,0);
	if(Ouvert){
		lcd.print(F("Porte Ouverte"));
	}
	else{
		lcd.print(F("Porte Ferm"));
		lcd.write(0);
		lcd.print(F("e"));
	}
	lcd.setCursor(15,0);
	if(config.Auto){
		lcd.write(65);// A
	}
	else{
		lcd.write(77);// M
	}
}
//---------------------------------------------------------------------------
void lcd0(){
	// 2eme ligne du menu 0
	lcd.setCursor(1,1);
	lcd.write(3);
	lcd.setCursor(5,1);
	lcd.print(F("Suivant"));
	lcd.setCursor(14,1);
	lcd.write(2);
}
//---------------------------------------------------------------------------
int menu3(int menu){	// Gestion keyLight
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("Seuil Lux :"));
	
	if(config.keyLight){
		//Serial.print(F("Seuil Lux")),//Serial.print(F("    ")),//Serial.println(F("Oui"));
		lcd10(true);//Oui,1ligne
	}
	else{
		//Serial.print(F("Seuil Lux")),//Serial.print(F(" ")),//Serial.println(F("Non"));
		lcd10(false);//Non,1ligne
	}
	lcd_Fin_Suiv_Regl();
		
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu100(menu);
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}
//---------------------------------------------------------------------------
int menu11(int menu){	// Gestion Auto/Man
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F(" Porte"));
	lcd_Auto_Manu(config.Auto);
	
	lcd_Fin_Suiv_Regl();
		
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu110(menu);
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}
//---------------------------------------------------------------------------
int menu110(int menu){	// Parametrage Auto/Man
	boolean Auto = config.Auto;
	lcd.setCursor(0,1);
	lcd.print(F("Auto Valider Man"));	

	do{
		if(Bp00){
			resetBp();
			//enregistrement en EEPROM
			if(Auto != config.Auto){
				config.Auto = Auto;
				EcrireEEPROM(10);
				record();
			}
			if(menu < max_menu){
				menu ++;
				Bp00 = true;
			} 
			else{
				menu = 99;
				Bp00 = true;
			}
			break;
		}
		if(Bp1){
			resetBp();
			Auto = !Auto;
			
			lcd_Auto_Manu(Auto);
		}
		if(Bp2){
			resetBp();
			Auto = !Auto;
			
			lcd_Auto_Manu(Auto);
		}
	}while(!timeOut);
	return menu;
}
//---------------------------------------------------------------------------
void lcd_Auto_Manu(boolean Auto){
	lcd.setCursor(12,0);
	if(Auto){
		lcd.print(F("Auto"));
	}
	else{
		lcd.print(F("Manu"));
	}
}
//---------------------------------------------------------------------------
int menu100(int menu){	// Parametrage keyLight
	boolean actif = config.keyLight;
	lcd_l2bis();
	
	if(actif){
		//Serial.print(F("Seuil Lux")),//Serial.print(F("    ")),//Serial.println(F("Oui"));
		lcd10(true);//Oui
	}
	else{
		//Serial.print(F("Seuil Lux")),//Serial.print(F(" ")),//Serial.println(F("Non"));
		lcd10(false);//Non
	}
	
	lcd.blink();
	lcd.setCursor(15,0);
	
	do{
		if(Bp00){
			//Serial.println(F("menu100 Bp00 pressed"));			
			resetBp();
			lcd.noBlink();
			//enregistrement en EEPROM
			if(actif != config.keyLight){
				config.keyLight = actif;
				EcrireEEPROM(10);
				record();
			}
			if(menu < max_menu){
				menu ++;
				Bp00 = true;
			} 
			else{
				menu = 99;
				Bp00 = true;
			}
			break;
		}
		if(Bp1){
			resetBp();
			actif = !actif;
			
			if(actif){
				//Serial.print(F("Seuil Lux")),//Serial.print(F("    ")),//Serial.println(F("Oui"));
				lcd10(true);//Oui
			}
			else{
				//Serial.print(F("Seuil Lux")),//Serial.print(F(" ")),//Serial.println(F("Non"));
				lcd10(false);//Non
			}
			lcd.setCursor(15,0);
		}
		if(Bp2){
			resetBp();
			actif = !actif;
			if(actif){
				//Serial.print(F("Seuil Lux")),//Serial.print(F("    ")),//Serial.println(F("Oui"));
				lcd10(true);//Oui
			}
			else{
				//Serial.print(F("Seuil Lux")),//Serial.print(F(" ")),//Serial.println(F("Non"));
				lcd10(false);//Non
			}
			lcd.setCursor(15,0);
		}
	}while(!timeOut);
	return menu;
}
//---------------------------------------------------------------------------
int menu5(int menu){	// Regler heure debut nuit
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("Fermeture"));
	lcd.setCursor(11,0);
	lcd.print(hdectohms(config.HDebutNuit));
	//Serial.print(F("H Fermer")),//Serial.println(hdectohms(config.HDebutNuit));
	lcd_Fin_Suiv_Regl();
	
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu200(menu,1);			
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}
//---------------------------------------------------------------------------
int menu6(int menu){	// Regler heure fin nuit
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("Ouverture"));
	lcd.setCursor(11,0);
	lcd.print(hdectohms(config.HFinNuit));
	//Serial.print(F("H Ouvert ")),//Serial.println(hdectohms(config.HFinNuit));
	lcd_Fin_Suiv_Regl();
	
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu200(menu,2);			
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}
//---------------------------------------------------------------------------
int menu200(int menu, int smenu){
	// smenu = 1 Heure début de nuit
	// smenu = 2 Heure fin de nuit
	// smenu = 3 Heure actuelle RTC
	int i  = 0;
	int hh = 0;
	int mm = 0;
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	
	if(i == 0){// traitement hh
		if(smenu == 1){	// Heure début de nuit
			hh = myHour(int(config.HDebutNuit / 3600));
			mm = int((config.HDebutNuit % 3600) / 60);
			//Serial.print(F("H Fermer")),//Serial.println(hdectohms(config.HDebutNuit));
			lcd.print(F("H Fermetur"));
			printval(hh,11);
			lcd.setCursor(13,0);
			lcd.print(":");
			printval(mm,14);
		}
		else if (smenu == 2){	// Heure fin de nuit
			hh = myHour(int(config.HFinNuit / 3600));
			mm = int((config.HFinNuit % 3600) / 60);
			//Serial.print(F("H Ouvert ")),//Serial.println(hdectohms(config.HFinNuit));
			lcd.print(F("H Ouvertur"));
			printval(hh,11);
			lcd.setCursor(13,0);
			lcd.print(":");
			printval(mm,14);
		}
		else if (smenu == 3){	// Heure actuelle RTC
			hh = myHour(hour());
			mm = minute();
			lcd.print(F("Actuelle"));
			printval(hh,11);
			lcd.setCursor(13,0);
			lcd.print(":");
			printval(mm,14);
		}
		lcd_l2bis();
		lcd.blink();
		lcd.setCursor(12,0);
		//Serial.print(F("  -  ")),//Serial.print(F(" valider ")),//Serial.println(F("  +  "));
	}

	do{
		if (Bp00){
			//Serial.print(i),//Serial.println(F(" menu200 Bp0 pressed"));			
			resetBp();
			if(i == 0){
				i = 1;
				if(smenu == 1){
					//Serial.print(F("H Fermer"));
				}
				else if (smenu == 2){
					//Serial.print(F("H Ouvert "));
				}
				else if (smenu == 3){
					//Serial.print(F("Actuelle "));
				}
				//Serial.print(hh),//Serial.print(F(":")),//Serial.print(mm),//Serial.println(F(":00"));
				//Serial.print(F("  -  ")),//Serial.print(F(" valider ")),//Serial.println(F("  +  "));
				
				lcd_l2bis();
				lcd.setCursor(15,0);
			}
			else if(i == 1){
				// on enregistre nouvelle données				
				if(smenu == 1){
					hh = corrigeHour(hh);
					if(config.HDebutNuit != hmstodect(hh,mm)){
						config.HDebutNuit = hmstodect(hh,mm);
						EcrireEEPROM(10);						// EEPROM
						record();
					}
				}
				else if(smenu == 2){
					hh = corrigeHour(hh);
					if(config.HFinNuit != hmstodect(hh,mm)){
						config.HFinNuit = hmstodect(hh,mm);
						EcrireEEPROM(10);						// EEPROM
						record();
					}
				}
				else if(smenu == 3){
					hh = corrigeHour(hh);
					setTime(hh,mm,00,day(),month(),year());
					// setTime(23, 31, 30, 24, 2, 2018);   //set the system time to 23h31m30s on 13Feb2009
					RTC.set(now());                     	 //set the RTC from the system time
					record();
				}
				Bp00 = true;
				lcd.noBlink();
				goto sortie20;
				// break;
			}
		}
		if (Bp1){
			resetBp();
			if(i == 0){ //hh--
				if(hh > 0 ){
					hh --;
					printval(hh,11);
				}
				else{
					hh = 23;
					printval(hh,11);
				}
			}
			if(i == 1){//mm--
				if(mm > 0 ){
					mm --;
					printval(mm,14);
				}
				else{
					mm = 59;
					printval(mm,14);
				}
			}
			//Serial.print(F("hh = ")),//Serial.print(hh),//Serial.print(F(" mm = ")),//Serial.println(mm);
		}
		if (Bp2){
			resetBp();
			if(i == 0){ //hh++
				if(hh < 23){
					hh ++;
					printval(hh,11);
				}
				else{
					hh = 0;
					printval(hh,11);
				}
			}
			if(i == 1){//mm++
				if(mm < 59){
					mm ++;
					printval(mm,14);
				}
				else{
					mm = 0;
					printval(mm,14);
				}
			}
			//Serial.print(F("hh = ")),//Serial.print(hh),//Serial.print(F(" mm = ")),//Serial.println(mm);			
		}
		
		Timer_Affichage();
	}while(!timeOut);
	sortie20:
	return menu;		
}
//---------------------------------------------------------------------------
void printval(int val,byte pos){
	// print sur lcd une valeur à une position
	lcd.setCursor(pos,0);
	if(val < 10)lcd.print("0");
	lcd.print(val);
	lcd.setCursor(pos+1,0);
}
//---------------------------------------------------------------------------
int menu7(int menu){	// Regler Date RTC
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("Date"));
	lcd.setCursor(6,0);
	char bidon[11];
	sprintf(bidon,"%02d/%02d/%04d",day(),month(),year());
	lcd.print(bidon);
	lcd_Fin_Suiv_Regl();
	
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu400(menu);			
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}	
//---------------------------------------------------------------------------
int menu8(int menu){	// Regler Heure RTC
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("Heure"));
	char bidon[9];
	lcd_Fin_Suiv_Regl();
	
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu200(menu,3);			
		}
		Timer_Affichage();
		
		lcd.setCursor(8,0);	
		sprintf(bidon,"%02d:%02d:%02d",myHour(hour()),minute(),second());
		lcd.print(bidon);
	}while(!timeOut);
	return menu;
}	
//---------------------------------------------------------------------------
int menu400(int menu){ // traitement date RTC
	int i = 0;
	int yyyy = 0;
	int mm = 0;
	int dd = 0;
	// lcd.clear();
	// lcd.backlight();
	// lcd.setCursor(0,0);
	
	if(i == 0){// traitement dd
		yyyy = year();
		mm   = month();
		dd   = day();
		
		lcd_l2bis();
		lcd.blink();
		lcd.setCursor(7,0);
	}

	do{
		if (Bp00){
			//Serial.print(i),//Serial.println(F(" menu400 Bp0 pressed"));			
			resetBp();
			if(i == 0){
				i = 1;
				lcd.setCursor(10,0);
				//Serial.print(F("Actuelle "));
				//Serial.print(dd),//Serial.print(F("/")),//Serial.print(mm),//Serial.print(F("/")),//Serial.println(yyyy);		
				//Serial.print(F("  -  ")),//Serial.print(F(" valider ")),//Serial.println(F("  +  "));
			}
			else if(i == 1){ // traitement mm
				i = 2;
				lcd.setCursor(15,0);
				//Serial.print(F("Actuelle "));
				//Serial.print(dd),//Serial.print(F("/")),//Serial.print(mm),//Serial.print(F("/")),//Serial.println(yyyy);		
				//Serial.print(F("  -  ")),//Serial.print(F(" valider ")),//Serial.println(F("  +  "));
			}
			else if(i == 2){ // validation
				// on enregistre nouvelle données		
				setTime(hour(),minute(),second(),dd,mm,yyyy);
				// setTime(23, 31, 30, 24, 2, 2018);   //set the system time to 23h31m30s on 13Feb2009
				RTC.set(now());                     //set the RTC from the system time
				record();
				
				Bp00 = true;
				lcd.noBlink();
				goto sortie40;
				// break;
			}
			
		}
		if (Bp1){
			resetBp();
			if(i == 0){ //dd--
				if(dd > 1 ){
					dd --;
					printval(dd,6);
				}
				else{
					dd = 31;
					printval(dd,6);
				}
			}
			if(i == 1){//mm--
				if(mm > 1 ){
					mm --;
					printval(mm,9);
				}
				else{
					mm = 1;
					printval(mm,9);
				}
			}
			if(i == 2){//yyyy--
				yyyy --;
				lcd.setCursor(12,0);
				lcd.print(yyyy);
				lcd.setCursor(15,0);
			}
			 //Serial.print(F("dd = ")),//Serial.print(dd),//Serial.print(F(" mm = ")),//Serial.print(mm),//Serial.print(F(" yyyy = ")),//Serial.println(yyyy);
		}
		if (Bp2){
			resetBp();
			if(i == 0){ //dd++
				if(dd < 30){
					dd ++;
					printval(dd,6);
				}
				else{
					dd = 1;
					printval(dd,6);
				}
			}
			if(i == 1){//mm++
				if(mm < 11){
					mm ++;
					printval(mm,9);
				}
				else{
					mm = 12;
					printval(mm,9);
				}
			}
			if(i == 2){//yyyy++
				yyyy ++;
				lcd.setCursor(12,0);
				lcd.print(yyyy);
				lcd.setCursor(15,0);
			}
			//Serial.print(F("dd = ")),//Serial.print(dd),//Serial.print(F(" mm = ")),//Serial.print(mm),//Serial.print(F(" yyyy = ")),//Serial.println(yyyy);			
		}
		
		Timer_Affichage();
		
	}while(!timeOut);
	sortie40:
	return menu;		
}		
//---------------------------------------------------------------------------		
int menu9(int menu){	// Calibration porte
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("CalibrationPorte"));
	
	//Serial.println(F("Calibration Porte "));
	lcd_Fin_Suiv_Regl();
		
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu600(menu);
		}
		
		Timer_Affichage();	
	
	}while(!timeOut);
	return menu;
}	
//---------------------------------------------------------------------------		
int menu600(int menu){	// Calibration porte
	byte i   = 0;	// sous menu
	byte pas = 2;	// 
	/*
	i=0 Porte Fermée? ajuste finement +-1 * 1/2t
	i=1 Ouverture pas à pas 					+-pas * 1/2t comptage->Cpto
	i=2 Fermeture 										cpto, ajustement fin +-1 * 1/2t ->Cptf
	i=3 Ouverture											cpto, ajustement fin +-1 * 1/2t ->Cpto
	i=4 Enregistrement et sortie	
	*/
	int cpto = 0;	// compteur nombre pression ouvrir
	int cptf = 0;	// compteur nombre pression fermer
	
	lcd.setCursor(0,0);
	lcd.print(F("  Porte Ferm"));
	lcd.write(0);
	lcd.print(F("e ? "));
	// lcd.print(F("  Porte Fermee ? "));
	lcd_l2m600();
	//Serial.println(F("  Porte Fermee ? "));
	//Serial.print(F("    ")),//Serial.print(F(" oui ")),//Serial.println(F(""));
	do{
		if (Bp00){
			resetBp();
			if(i == 0){// premier passage valide que porte bien fermée
				i = 1;
				lcd.setCursor(0,0);
				lcd.print(F("Ouvertur pas/pas"));				
				lcd_l2m600();
				lcd.blink();
				lcd.setCursor(1,1);
			}
			else if(i == 1){// 2eme passage valide fin ouverture
				i = 2;
				Serial.print("avant validation "),Serial.print(cpto),Serial.print(","),Serial.println(cptf);
				cpto *= pas; 		// calcul nombre 1/2t
				cptf = cpto;
				lcd.setCursor(0,0);
				lcd.print(F("    Fermeture   "));
				lcd_l2m600();
				Serial.print("apres validation "),Serial.print(cpto),Serial.print(","),Serial.println(cptf);
				Moteur(false,cptf,false,false);//fermeture
			}
			else if(i == 2){
				i = 3;
				lcd.setCursor(0,0);
				lcd.print(F("    Ouverture   "));
				lcd_l2m600();
				Serial.print(cpto),Serial.print(","),Serial.println(cptf);
				Moteur(true,cpto,false,false);//ouverture
			}	
			else if(i == 3){
				if((cpto > 0) && (cptf > 0) && (cpto != config.N_Ouverture || cptf != config.N_Fermeture)){
					config.N_Ouverture = cpto;
					config.N_Fermeture = cptf;	
					config.PorteOuverte = true;
					EcrireEEPROM(10); //sauv EEPROM
				}
				record();
				if(menu < max_menu){
					menu ++;
					Bp00 = true;
				} 
				else{
					menu = 99;
					Bp00 = true;
					
				}
				lcd.noBlink();
				goto sortie60;
				// break;
			}
		}
		else if (Bp1){		
			resetBp();
			if(i == 0){													// positionnement fin de la poulie 1s
				Moteur(true,1,false,false);//ouverture
			}
			else if(i == 1){ // incrementation du pas
				cpto ++;
				lcd.setCursor(2,1);
				lcd.print(cpto * pas);
				lcd.setCursor(2,1);
				Moteur(true,pas,false,false);//ouverture
				//Serial.print(F("Ouverture = ")),//Serial.println(cpto);
			}
			else if(i == 2){//
				cptf -= 1;
				Moteur(true,1,false,false);//ouverture
			}
			else if(i == 3){//
				cpto += 1;
				Moteur(true,1,false,false);//ouverture
			}
		}
		else if (Bp2){
			//Serial.println(F("menu600 Bp2 pressed"));
			resetBp();
			if(i == 0){													// positionnement fin de la poulie 1s
				Moteur(false,1,false,false);//fermeture
			}
			else if(i == 1){ // decrementation du pas
				if(cpto > 2)cpto --;
				lcd.setCursor(2,1);
				lcd.print(cpto * pas);
				lcd.setCursor(2,1);
				Moteur(false,pas,false,false);//fermeture
				//Serial.print(F("Ouverture = ")),//Serial.println(cpto);
			}
			else if(i == 2){// 
				cptf += 1;
				Moteur(false,1,false,false);//fermeture
			}
			else if(i == 3){// 
				cpto -= 1;
				Moteur(false,1,false,false);//fermeture
			}
		}
		
		Timer_Affichage();	
	
	}while(!timeOut);
	sortie60:
	return menu;
}
//---------------------------------------------------------------------------		
void lcd_l2m600(){
	lcd.setCursor(0,1);
		for(byte j = 0;j < 16;j++){ // efface ligne 2 lcd
			lcd.write(32);
		}
	lcd.setCursor(1,1);
	lcd.write(3); // fleche montante
	lcd.setCursor(7,1);
	lcd.print(F("OK"));
	lcd.setCursor(13,1);
	lcd.write(2); // fleche descendante
}
//---------------------------------------------------------------------------	
int menu1(int menu){	// Affichage Etat Batterie
	digitalWrite(OpMesureLight, HIGH);
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("Batterie "));
	lcd.setCursor(15,0);
	lcd.write(37);//%
	lcd.setCursor(0,1);
	lcd.print(F("Fin  Suiv       "));
	
	char bidon[4];
	do{
		
		sprintf(bidon,"%03d",batpct());
		lcd.setCursor(11,0);
		lcd.print(bidon);			
		
		if (Bp00){
			resetBp();
			digitalWrite(OpMesureLight, LOW);
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			break;
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}	
//---------------------------------------------------------------------------		
int menu2(int menu){	// Affichage Lux mesuré
	digitalWrite(OpMesureLight, HIGH);
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.print(F("Mesure Lux"));
	lcd.setCursor(15,0);
	lcd.write(37);//%
	// //Serial.println(F("Mesure lux "));
	lcd.setCursor(0,1);
	lcd.print(F("Fin  Suiv       "));
	uint32_t timer = millis();
	unsigned int lum = 0;
	unsigned int cpt = 0;
	char bidon[4];
	do{		
		if(millis() - timer > 200){
			cpt ++;
			lum += map(analogRead(A0),0,1023,0,100);
			timer = millis();
			sprintf(bidon,"%02d",lum/cpt);
			lcd.setCursor(11,0);
			lcd.print(bidon);	
			if(cpt > 10){
				cpt = 0;
				lum = 0;
			}
		}
		
		if (Bp00){
			resetBp();
			digitalWrite(OpMesureLight, LOW);
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			break;
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}	
//---------------------------------------------------------------------------		
int menu4(int menu){	// Reglage seuil lux
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0,0);
	lcd.write(82);//R
	lcd.write(0);
	lcd.print(F("glage Lux"));
	lcd.setCursor(12,0);
	lcd.print(config.seuilJour);
	lcd.setCursor(15,0);
	lcd.write(37);//%
	//Serial.println(F("Reglage Seuil Lux "));
	lcd_Fin_Suiv_Regl();
		
	do{
		if (Bp00){
			resetBp();
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			resetBp();
			menu = ActionBp1(menu); // sortir
			break;
		}
		if (Bp2){
			resetBp();
			menu = menu800(menu);
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}	
//---------------------------------------------------------------------------		
int menu800(int menu){	// Reglage seuil lux
	int seuil = config.seuilJour;	// seuil
		
	lcd_l2bis();
	
	//Serial.print(F("Reglage Seuil Lux = ")),//Serial.print(config.seuilJour),//Serial.println(F("%"));	
	//Serial.print(F("  -  ")),//Serial.print(F(" Suivant ")),//Serial.println(F("  +  "));
	
	do{
		if (Bp00){
			//Serial.print(menu),//Serial.println(F("menu800 Bp0 pressed"));
			resetBp();
			//Serial.print(F("Seuil Lux = ")),//Serial.println(seuil);
			if(config.seuilJour != seuil){
				config.seuilJour = seuil;
				EcrireEEPROM(10);//sauv EEPROM
				record();
			}
			
			if(menu < max_menu){
				menu ++;
				Bp00 = true;
			} 
			else{
				menu = 99;
				Bp00 = true;
			}
			goto sortie80;
			// break;
		}
		if (Bp1){
			//Serial.println(F("menu800 Bp1 pressed"));
			resetBp();
			if(seuil > 0){
				seuil --;
				lcd.setCursor(12,0);
				lcd.print(seuil);
			}
			else{
				seuil = 100;
				lcd.setCursor(12,0);
				lcd.print(seuil);
			}
			//Serial.print(F("Seuil Lux = ")),//Serial.print(seuil),//Serial.println(F("%"));
		}
		if (Bp2){
			//Serial.println(F("menu800 Bp2 pressed"));			
			resetBp();
			if(seuil < 100){
				seuil ++;
				lcd.setCursor(12,0);
				lcd.print(seuil);
			}
			else{
				seuil = 0;
				lcd.setCursor(12,0);
				lcd.print(seuil);
			}
			//Serial.print(F("Seuil Lux = ")),//Serial.print(seuil),//Serial.println(F("%"));
		}
		Timer_Affichage();
	}while(!timeOut);
	sortie80:
	return menu;
}
//---------------------------------------------------------------------------		
int menu10(int menu){	// About
	lcd.clear();
	lcd.backlight();
	lcd.setCursor(0, 0);
	lcd.print(F("Poulailler Auto"));
	lcd.setCursor(0,1);
	lcd.print(F("V"));
	
	lcd.print(ver);
	
	//Serial.println(F("Poulailler Auto"));
	//Serial.print(F("PhC V")),//Serial.println(ver);
	// lcd_Fin_Suiv_Regl();
		
	do{
		if (Bp00){
			menu = ActionBp00(menu);
			break;
		}
		if (Bp1){
			menu = ActionBp00(menu);
			break;
		}
		if (Bp2){
			menu = ActionBp00(menu);
			break;
		}
		Timer_Affichage();
	}while(!timeOut);
	return menu;
}		
//---------------------------------------------------------------------------		
void pciSetup(byte pin){	// armement pin interrupt
	//  http://www.gammon.com.au/interrupts
	*digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
	PCIFR  |= bit (digitalPinToPCICRbit(pin)); // clear any outstanding interrupt
	PCICR  |= bit (digitalPinToPCICRbit(pin)); // enable interrupt for the group
}
//---------------------------------------------------------------------------
void Timer_Affichage() {// time out menu
  if (StartTimerAffichage) {
    ////Serial.print(F("StartTimerAffichage =")),//Serial.println(StartTimerAffichage);
    timer2 = millis();
    StartTimerAffichage = false;
  }
  //  //Serial.print(StartTimerAffichage),//Serial.print(F("|")),//Serial.println(millis() - timer2);
  if (millis() - timer2 > timeout) {
    timer2 = millis();
    StartTimerAffichage = true;
    timeOut = true;               // sortir ecran en cours
		//Serial.println(F("Time out menu"));
		return;
  }
  return ;
}
//---------------------------------------------------------------------------
void resetBp(){
	Bp00 = false;
	Bp1  = false;
	Bp2  = false;	
	StartTimerAffichage = true;
}
//---------------------------------------------------------------------------
int ActionBp1(int menu){
	resetBp();
	menu = 99;
	timeOut = true;
	return menu;
}
//---------------------------------------------------------------------------
int ActionBp00(int menu){	
	//Serial.print(F("menu ")),//Serial.print(menu),//Serial.println(F(" Bp0 pressed"));			
	resetBp();
	if(menu < max_menu){
		menu ++;
	} 
	else{
		menu = 99;
	}
	return menu;
}
//---------------------------------------------------------------------------
void lcd_l2bis(){
	lcd.setCursor(0,1);
	lcd.print(F(" -  valider  +  "));
}
//---------------------------------------------------------------------------
void lcd_Fin_Suiv_Regl(){
	lcd.setCursor(0,1);
	lcd.print(F("Fin  Suiv  Regl."));
	lcd.setCursor(12,1);
	lcd.write(0);
}
//---------------------------------------------------------------------------
String hdectohms(long t){
	// convertir heure décimale en hh:mm:ss
	String hms;
	if(myHour(int(t / 3600)) < 10) hms = "0";
	hms += myHour(int(t / 3600));
	hms += ":";	
	if(int((t % 3600) / 60) < 10) hms += "0";
	hms += int((t % 3600) / 60);
	//hms += ":00";
	
	return hms;
}
//---------------------------------------------------------------------------
long hmstodect(int h, int m){
	// convertir hh mm en heure decimale
	long d = h*60;
	d += m;
	d *= 60;
	return d;
}
//---------------------------------------------------------------------------
void PrintEEPROM(){
	Serial.print(F("numero magic = ")),Serial.println(config.magic);
	Serial.print(F("Heure debut de nuit = ")),Serial.println(config.HDebutNuit);
	Serial.print(F("Heure fin de nuit = ")),Serial.println(config.HFinNuit);
	Serial.print(F("niveau lum ferme le soir = ")),Serial.println(config.keyLight);
	Serial.print(F("seuil lum  = ")),Serial.println(config.seuilJour);
	Serial.print(F("N_Fermeture  = ")),Serial.println(config.N_Fermeture);
	Serial.print(F("N_Ouverture  = ")),Serial.println(config.N_Ouverture);
	Serial.print(F("PorteOuverte  = ")),Serial.println(config.PorteOuverte);
	Serial.print(F("Auto  = ")),Serial.println(config.Auto);
	Serial.print(F("addr lcd = ")),Serial.println(Lcd_Adr);
	Serial.flush();
}
//---------------------------------------------------------------------------
void EcrireEEPROM(byte adr){
	while (!eeprom_is_ready());
	int longueur = EEPROM_writeAnything(adr, config);	// ecriture des valeurs par defaut
	delay(10);
	// Serial.print(F("longEEPROM=")),Serial.println(longueur);
}
//---------------------------------------------------------------------------
long readVcc() {
	// https://provideyourown.com/2012/secret-arduino-voltmeter-measure-battery-voltage/
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both

  long result = (high<<8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}
//---------------------------------------------------------------------------
byte batpct(){// retourne etatbatt en %
	int echelbatt[12] = {4150,4100,3970,3920,3870,3830,3790,3750,3700,3600,3300,3000};
	byte pctbatt [12] = {100,90,80,70,60,50,40,30,20,10,5,0};
	long V_Batterie = readVcc();
	byte EtatBat=0;
	
	for(int i = 0; i < 9; i++){// on fait une moyenne
		V_Batterie += readVcc();
	}	
	V_Batterie /=10;
	
	delay(100);
	Serial.print("V Batt :"),Serial.println(V_Batterie);
	for(int i = 0; i < 12; i++){
		if(V_Batterie > echelbatt[i]){
			EtatBat = pctbatt[i];
			return EtatBat;
		}
	}
	return 0;
}
//---------------------------------------------------------------------------
boolean HeureEte() {
  // return true en été, false en hiver (1=dimanche)
  boolean Hete = false;
  if (month() > 10 || month() < 3
      || (month() == 10 && (day() - weekday()) > 22)
      || (month() == 3  && (day() - weekday()) < 24)) {
    Hete = false;                      								// c'est l'hiver
  }
  else {
    Hete = true;                       								// c'est l'été h+1
  }
  return Hete;
}
//---------------------------------------------------------------------------
byte myHour(byte heure){	// corrige heure ete/hiver
	if(HeureEte()){
		if(heure < 23){
			heure ++;
		}
		else{
			heure = 0;
		}
	}
	return heure;
}
//---------------------------------------------------------------------------
byte corrigeHour(byte heure){	// heure locale vers UTC
	if(HeureEte()){	// en ete enlever 1h avant ecrire horloge
		if(heure > 0){
			heure --;
		}
		else{
			heure = 23;
		}
	}
return heure;
}
//---------------------------------------------------------------------------
void ledcligno(int cpt){
	for(int i = 0;i < cpt;i++){
		digitalWrite(OpLEDRouge,HIGH);
		delay(30);
		digitalWrite(OpLEDRouge,LOW);
		if(cpt > 1) delay(200);
	}
}
//---------------------------------------------------------------------------
void record(){
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.print(F("   Enregistr"));
	lcd.write(0);
	lcd.print(F("   "));
	delay(1500);	
}
//---------------------------------------------------------------------------
void lcd10(boolean Oui){
	// Oui/Non
	lcd.setCursor(13,0);
	if(Oui){lcd.print(F("Oui"));}
	else{lcd.print(F("Non"));}
}
