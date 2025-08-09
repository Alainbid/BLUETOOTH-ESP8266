#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include "secrets.h" // Include your secrets.h file for WiFi credentials

#define EEPROM_SIZE 512 // Define the size of EEPROM (ESP8266 has up to 512 bytes of EEPROM)
#define STRING_ADDR 0   // Start address in EEPROM
#define CHECKSUM_ADDR (STRING_ADDR + 500) // Assuming the data length is within 300 bytes

String data = "21010$1/21020/9/0/&4/20995/5/0/&4/21050/5/1/&3/21040/5/0/&2/21060/5/0/&2/21025/3/1/"; // Example data to store  
//tous les temps sont en secondes
int dataSize= data.length();
bool test = true;
String tableNomZones[] = {"Potager   ", "Parking   ", "Portillon", "Grand Tour"};


#define SOFT_RX D7  // The ESP8266 pin connected to the TX of the Bluetooth module
#define SOFT_TX D6  // The ESP8266 pin connected to the RX of the Bluetooth module
SoftwareSerial bluetooth(SOFT_RX, SOFT_TX);

// Informations sur le réseau Wi-Fi
const char* ssid = "michal";
const char* password = "bellecombe";
// Créez un objet WiFiUDP pour recupérer l'heure actuelle
WiFiUDP ntpUDP;
// Créez un objet NTPClient pour interroger un serveur NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // décalage en secondes et intervalle de mise à jour
unsigned long epochTime = 0L;

// Define the relay pins
const int relayPins[] = {D1, D2, D3, D4};
// nombre de relay
const int numRelays = sizeof(relayPins) / sizeof(relayPins[0]);

int compteur = 0;
unsigned long attente = 0;
int debut = 0;
int fois[] = {0, 0, 0, 0};
String command = "";
int actuel = 0;
int relayCommandCount = 0;

struct RelayCommand {
  int relayNbr; //N° du relay
  int start;  //heure début en s depuis 0h00
  unsigned long duration; // durée en secondes
  bool inProgress; // nouvel état
  unsigned long startTime; // début en millisecondes
  bool done;
};
RelayCommand relayCommands[12]; //4 relais x 3 fois

String getTimeString() {
  timeClient.update();
  int hours = timeClient.getHours() +2; // Ajout de 2 heures pour l'heure locale (UTC+2)
  int minutes = timeClient.getMinutes();
  int seconds = timeClient.getSeconds();

  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%02d:%02d.%02d", hours, minutes, seconds);
  return String(buffer);
}

unsigned int calculateChecksum(String data) {
  unsigned int checksum = 0;
      for (unsigned int i = 0; i < data.length(); i++) {
        checksum += data[i];
      }
  return checksum;
}

void sortRelayCommandsByStart(RelayCommand arr[], int n) {
  // Bubble sort to sort the relay commands by start time
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - i - 1; j++) {
      if (arr[j].start > arr[j + 1].start) {
        RelayCommand temp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = temp;
      }
    }
  }
}

//stockage de la commande dans EEPROM
void storeDataWithChecksum(String data) {
   dataSize = data.length();
 
  // Write string to EEPROM
    for (int i = 0; i < dataSize; i++) {
      EEPROM.write(STRING_ADDR + i, data[i]);
    }
  EEPROM.write(STRING_ADDR + dataSize, '\0'); // Null-terminate the string
  
  // Calculate and store checksum
  unsigned int checksum = calculateChecksum(data);
  EEPROM.put(CHECKSUM_ADDR, checksum);
  EEPROM.commit(); // Save changes to EEPROM
  if(test){ 
    Serial.println(" taille des datas " + String(dataSize));
    Serial.println(" data écris dans EEPROM " + data); 
    Serial.println(" checksum ecrite dans EEPROM " + String(checksum));
  }
}

//lecture de la dernière commande mémorisée en EEPROM
String retrieveDataWithVerification() {
  char readData[512];
  // Read string from EEPROM
  for(int i = 0; i< dataSize ; i++){
    readData[i] = EEPROM.read(STRING_ADDR + i);
  }
  readData[dataSize] = '\0'; //le dernier cractère doit être '/0'
  // Read stored checksum
  unsigned int storedChecksum;
  EEPROM.get(CHECKSUM_ADDR, storedChecksum);
  // Calculate checksum of retrieved data
  String retrievedData = String(readData);
  if(test) Serial.println(" data lus dans EEPROM " + retrievedData);
  unsigned int calculatedChecksum = calculateChecksum(retrievedData);
  Serial.println(" checksum calculée  " + String(calculatedChecksum));
  //Verify checksum
  if (storedChecksum == calculatedChecksum) {
    Serial.println(" data lus dans EEPROM " + retrievedData);
  }
  else{
    Serial.println(" data ERREUR de checksum " + retrievedData);
  }
  return retrievedData;
}



//exemple de  commande  "21000$1/21020/10/0/&4/21035/8/0/&2/21045/5/0/&"
// chaque block de commande est délimité par le caractère :  &
// il peur y avoir jusqu'à 4 relais  x  3 repetitions possibles
// composition du bloc de commande : 
//relay numéro / heure départ en secondes / durée en secondes / répétitions 1 2 3
void parseCommand(String command) {
  
  String secondsDebut = "";
  relayCommandCount = 0;
  int endIndex = command.indexOf('$');
  // $ sépare le début de command de la programmation
  secondsDebut = command.substring(0, endIndex);
  Serial.println(" secondes debut " + secondsDebut);
  
  // permet de mettre compteur à l'heure
   compteur =  secondsDebut.toInt(); //********************************************************************** */
   Serial.println(" compteur" + String(compteur));
  unsigned int startIndex = 0;
  int blocIndex = command.indexOf('&');
  // on décompose les commandes
  startIndex = secondsDebut.length() + 1;

  while (startIndex < command.length()) { // fin de command ?
    if (blocIndex == -1) { // fin du bloc de commandes du relay
      blocIndex = command.length();
    }
    // découpage de chaque commandes du relay encours
    String block = command.substring(startIndex, blocIndex);
  Serial.println(" bloc " + block);
    int startSubIndex = 0;
    int endSubIndex = block.indexOf('/');

    // quel relay ?
    if (endSubIndex != -1) {
      relayCommands[relayCommandCount].relayNbr = block.substring(startSubIndex, endSubIndex).toInt();
      startSubIndex = endSubIndex + 1;
      endSubIndex = block.indexOf('/', startSubIndex);
    }
    // heure de début
    if (endSubIndex != -1) {
      relayCommands[relayCommandCount].start = block.substring(startSubIndex, endSubIndex).toInt();
      startSubIndex = endSubIndex + 1;
      endSubIndex = block.indexOf('/', startSubIndex);
    }
    // durée
    if (endSubIndex != -1) {
      relayCommands[relayCommandCount].duration = block.substring(startSubIndex, endSubIndex).toInt();
      startSubIndex = endSubIndex + 1;
      endSubIndex = block.indexOf('/', startSubIndex);
    }
    // nombre de fois 1,2,3 ?
    if (endSubIndex != -1) {
      //int nbrDeFois = block.substring(startSubIndex, endSubIndex).toInt();
      // On ne gère pas le nombre de fois pour l'instant
      relayCommands[relayCommandCount].inProgress = false;
      relayCommands[relayCommandCount].done = false;
    }
    relayCommandCount++;

    startIndex = blocIndex + 1; 
    // bloc de commande du relay suivant
    blocIndex = command.indexOf('&', startIndex);
  }

for(int i =0; i < relayCommandCount; i++){
  sortRelayCommandsByStart(relayCommands, relayCommandCount);
    Serial.println(  tableNomZones[relayCommands[i].relayNbr - 1] + "\t" +
             " : start = " + String(relayCommands[i].start) +
             ", duration = " + String(relayCommands[i].duration) +
             ", inProgress = " + String(relayCommands[i].inProgress) +
             ", done = " + String(relayCommands[i].done));
  }
  // trier les commandes par heure de début
}



void handleRelayCommand(RelayCommand &relayCmd) {
  int relayNumber  = relayCmd.relayNbr - 1; // on commence à indice 0 dans le tableau des relais
//  Serial.print( "relayNumber ");
//  Serial.println( relayNumber);
//  Serial.println( "compteur = " + String(compteur));
//  Serial.println( "relayCmd.start = " + String(relayCmd.start));
//  Serial.println( "fois[relayNumber] = " + String(fois[relayNumber]));
//  Serial.println( "relayCmd.inProgress = " + String(relayCmd.inProgress));

  if (relayNumber >= 0) {

    if (!relayCmd.inProgress && !relayCmd.done  && compteur == relayCmd.start) {
      digitalWrite(relayPins[relayNumber], LOW); // Turn relay on
      bluetooth.println(tableNomZones[relayNumber] +" -> " + " is ON for " + String(relayCmd.duration) + " seconds");
      Serial.println("\n\n" + tableNomZones[relayNumber] +" -> " + " is ON for " + String(relayCmd.duration) + " seconds");

      relayCmd.startTime = millis(); // Start timing
      relayCmd.inProgress = true;
    }

    if (relayCmd.inProgress && millis() - relayCmd.startTime >= relayCmd.duration * 1000) {
      digitalWrite(relayPins[relayNumber], HIGH); // Turn relay off
      bluetooth.println(tableNomZones[relayNumber] + " -> " + " is OFF");
      Serial.println("\n\n " + tableNomZones[relayNumber] +" -> " + " is OFF");

      relayCmd.inProgress = false;
      relayCmd.done = true; // Command is done
    }
  } else {
    Serial.println("Invalid relay number: " + String(relayNumber + 1));
  }
}

void setup() {
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  attente= millis();
  
  Serial.begin(115200);
  bluetooth.begin(9600);
  Serial.println("...");
  Serial.println("Bluetooth disponible à 9600 bds");

if (test){
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      int wifiTimeout = 0;
      while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
        delay(500);
        Serial.print("...");
        wifiTimeout++;
      }
      Serial.print("Adresse MAC de l'ESP8266: ");
      Serial.println(WiFi.macAddress());
      Serial.print("Adresse IP de l'ESP8266: ");
      Serial.println(WiFi.localIP());
  }
String heureLue = getTimeString();
Serial.println("\n\n" + heureLue); // Affiche par exemple : 14:05.09

  // Initialisez les pins des relais
  for (int i = 0; i < numRelays; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH); // Relays are off initially
  }
    
  if(test)  storeDataWithChecksum(data);
  // Read the stored data from EEPROM (assuming dataSize is 300)
  command = retrieveDataWithVerification();
  if(test) Serial.println("\n début commande stockée dans EEPROM :" + command);

  // Parse the command once
  parseCommand(command);
}


void loop() {

  if(bluetooth.available()){
 
      command="";
      // si données disponibles sur le bluetooth on lit la commande
     command = bluetooth.readStringUntil('\n'); // Read the command string until a newline character
      
     Serial.println(" command reçue "+command);
      int longueur = command.length();
      if(longueur > 0){
      String heureLue = getTimeString();
      Serial.println("\n\n" + heureLue); // Affiche par exemple : 14:05.09
      bluetooth.println("\n\n" + heureLue); // Affiche par exemple : 14:05.09

        if(command == "a"){
          //on a reçu la lettre 'a' lecture de EEPROM
          command = (retrieveDataWithVerification());
          Serial.println( "\nEEPROM = " + command  +"\n");
         bluetooth.println( command );
         //on veut mettre la programmation au minimum
        }else if(command == "z"){
          command = "3600$1/3615/120/0/&";
         storeDataWithChecksum(command);// backup dans EEPROM
      }else{
         bluetooth.println("Commandes reçues sur ESP32 : " + command);
          //on a reçu une commande
          storeDataWithChecksum(command);// backup dans EEPROM
          Serial.print("nombre de bytes  : " );
          Serial.println(longueur);
          Serial.println("commande  reçue : "+ command);
          parseCommand(command);

      }
    }
  }

  if (compteur > 21080) // si compteur > 21080 on le remet à 1
  {
    Serial.println("compteur remis à 21000");
    // on remet le compteur à 1
    // on remet les relais à l'état initial
    for (int i = 0; i < numRelays; i++)
    {
      digitalWrite(relayPins[i], HIGH); // Relays are off initially
      Serial.println(tableNomZones[i] + String(i + 1) + " is OFF");
    }

    for (int i = 0; i < 12; i++)
    {
      relayCommands[i].inProgress = false;
      relayCommands[i].done = false;
    }

    attente = millis();
    compteur = 20990;
    Serial.println("compteur remis à 21010");
  }
  // RAZ  du compteur à minuit
  

  // Add a delay for readability
  if(millis() >= attente + 1000)
  {
    compteur ++;
    attente = millis();
    Serial.print("."+String(compteur));
    }

  //Iterate over each relay command and execute it if conditions are met
  for (int i = 0; i < relayCommandCount; i++) {
    RelayCommand& relayCmd = relayCommands[i];
     handleRelayCommand(relayCmd);
  }

}


