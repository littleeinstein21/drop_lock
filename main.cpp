#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <Keypad.h>
#include <WiFiManager.h> // WiFiManager library
#include <WebServer.h>   // ESP32 Web Server library
#include <FirebaseESP32.h>

// Inisialisasi LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Fingerprint sensor
#define RX_PIN 16
#define TX_PIN 17
Adafruit_Fingerprint finger(&Serial2);

// Pin Relay
#define RELAY_PIN 18

// Keypad settings
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};
byte rowPins[ROWS] = {26, 27, 14, 12};
byte colPins[COLS] = {13, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Sensor Ultrasonik HC-SR04
#define TRIG_PIN 15
#define ECHO_PIN 2
long duration;
int distance;

// Variabel global
bool fingerprintError = false;
bool objectDetected = false;

// Firebase credentials
#define FIREBASE_HOST "https://tes-iot-7ad23-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "JJaBZAoOQjMe2zuFNcPkLYdfe7lDtK4q54DNFm8M"

FirebaseData fbData;
FirebaseConfig fbConfig;
FirebaseAuth fbAuth;

// Web server
WebServer server(80);

// Forward declaration untuk semua fungsi
void resetLCD();
void registerFingerprint();
void handleFingerprintError();
void unlockWithFingerprint();
void emergencyOpenDoor();
void openDoor();
void checkDistance();
void sendFirebaseNotification(int distance, bool prolonged);
void sendFingerprintUIDToFirebase(int uid);
void addFingerprintUIDToFirebase(int uid);
void checkDoorControl();
void clearAllFingerprints();
void resetWiFiManager();

void setup() {
    Serial.begin(115200);

    // Setup LCD
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Initializing...");
    delay(2000);

    // Setup WiFi using WiFi Manager
    WiFiManager wm;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Setup");
    lcd.setCursor(0, 1);
    lcd.print("Access Point");
    if (!wm.autoConnect("ESP32-Door")) { // AP mode jika gagal konek
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Failed!");
        delay(2000);
        ESP.restart();
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");

    // Setup Firebase
    fbConfig.host = FIREBASE_HOST;
    fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&fbConfig, &fbAuth);
    Firebase.reconnectWiFi(true);

    if (!Firebase.ready()) {
        Serial.println("Firebase setup failed!");
        Serial.println("Error Reason: " + fbData.errorReason());
        while (true) delay(1000);
    } else {
        Serial.println("Firebase ready!");
    }

    // Setup fingerprint sensor
    Serial2.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);
    if (!finger.verifyPassword()) {
        Serial.println("Fingerprint sensor not found.");
        lcd.setCursor(0, 0);
        lcd.print("Fingerprint Error");
        fingerprintError = true;
    }

    // Setup pin relay
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    // Setup sensor ultrasonik
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    // Setup web server
    server.on("/", []() {
        server.send(200, "text/plain", "ESP32 is running.");
    });
    server.begin();

    // Tampilan awal LCD
    resetLCD();
}

void loop() {
    server.handleClient();

    // Handle Keypad Input
    char key = keypad.getKey();

    if (key) {
        Serial.print("Key pressed: ");
        Serial.println(key);

        switch (key) {
            case 'A':
                if (!fingerprintError) {
                    registerFingerprint();
                } else {
                    handleFingerprintError();
                }
                break;
            case 'B':
                if (!fingerprintError) {
                    unlockWithFingerprint();
                } else {
                    handleFingerprintError();
                }
                break;
            case 'C':
                emergencyOpenDoor();
                break;
            case 'D':
                clearAllFingerprints();
                break;
            case '*':
                resetWiFiManager(); // Tambahkan fungsi reset WiFi Manager di sini
                break;
            default:
                Serial.println("Invalid key");
                break;
        }
    }
    resetLCD();
    // Check Distance and Send Notification if Object is Detected
    checkDistance();
    delay(500);

    // Check Firebase doorControl command
    checkDoorControl();
}
void resetLCD() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Press C");
    lcd.setCursor(0, 1);
    lcd.print("To Unlock");
}

void checkDoorControl() {
    if (Firebase.ready()) {
        String path = "/doorControl";
        if (Firebase.getString(fbData, path)) {
            String command = fbData.stringData();
            if (command == "unlock") {
                Serial.println("Unlock command received from app");
                openDoor(); // Buka pintu dan tampilkan "Door Closed"
                Firebase.setString(fbData, path.c_str(), "locked"); // Reset ke locked
            }
        } else {
            Serial.print("Failed to read doorControl: ");
            Serial.println(fbData.errorReason());
        }
    } else {
        Serial.println("Firebase not ready!");
    }
}
void clearAllFingerprints() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Clearing Data...");

    // Hapus semua data dari sensor fingerprint
    if (finger.emptyDatabase() == FINGERPRINT_OK) {
        lcd.setCursor(0, 1);
        lcd.print("FP Cleared");
        Serial.println("All fingerprints cleared from sensor.");
    } else {
        lcd.setCursor(0, 1);
        lcd.print("FP Clear Failed");
        Serial.println("Failed to clear fingerprints from sensor.");
    }

    delay(2000);

    // Hapus data dari Firebase
    if (Firebase.ready()) {
        String pathUIDs = "/fingerprint/registeredUIDs";
        String pathLastUID = "/fingerprint/lastUID";

        // Hapus UID yang terdaftar
        if (Firebase.deleteNode(fbData, pathUIDs)) {
            Serial.println("Registered UIDs cleared from Firebase.");
        } else {
            Serial.print("Failed to clear registered UIDs: ");
            Serial.println(fbData.errorReason());
        }

        // Hapus lastUID
        if (Firebase.deleteNode(fbData, pathLastUID)) {
            Serial.println("Last UID cleared from Firebase.");
        } else {
            Serial.print("Failed to clear last UID: ");
            Serial.println(fbData.errorReason());
        }

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Firebase Cleared");
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Firebase Error");
        Serial.println("Firebase not ready. Failed to clear data.");
    }

    delay(2000);
    resetLCD();
}

void resetWiFiManager() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Resetting WiFi");
    lcd.setCursor(0, 1);
    lcd.print("Please Wait...");
    delay(2000);

    WiFiManager wm;
    wm.resetSettings(); // Reset pengaturan WiFi yang tersimpan
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Reset!");
    lcd.setCursor(0, 1);
    lcd.print("Restarting...");
    delay(2000);
    
    ESP.restart(); // Restart ESP32 untuk memulai ulang proses konfigurasi
}

void checkDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    duration = pulseIn(ECHO_PIN, HIGH);
    distance = duration * 0.0344 / 2;

    if (distance < 15) {
        if (!objectDetected) {
            objectDetected = true;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Object Detected!");
            lcd.setCursor(0, 1);
            lcd.print("Distance: ");
            lcd.print(distance);
            lcd.print(" cm");
            Serial.print("Object detected at: ");
            Serial.print(distance);
            Serial.println(" cm - Sending data...");

            sendFirebaseNotification(distance, false);
        }
    } else {
        if (objectDetected) {
            objectDetected = false;
            resetLCD();
            Serial.println("Object no longer detected. Resetting LCD.");
        }
    }
}
void sendFirebaseNotification(int distance, bool prolonged) {
    if (Firebase.ready()) {
        String path = prolonged ? "/ultrasonic/prolongedNotification" : "/ultrasonic/notification";
        String message = prolonged
                             ? "Object detected <15cm for 5s"
                             : "Object detected at " + String(distance) + " cm";
        if (Firebase.setString(fbData, path.c_str(), message)) {
            Serial.println("Notification sent to Firebase: " + message);
        } else {
            Serial.print("Error sending to Firebase: ");
            Serial.println(fbData.errorReason());
        }
    }
}

void sendFingerprintUIDToFirebase(int uid) {
    if (Firebase.ready()) {
        String path = "/fingerprint/lastUID";
        if (Firebase.setInt(fbData, path.c_str(), uid)) {
            Serial.println("Fingerprint UID sent to Firebase successfully!");
        } else {
            Serial.print("Error sending UID to Firebase: ");
            Serial.println(fbData.errorReason());
        }
    } else {
        Serial.println("Firebase not ready!");
    }
}

void addFingerprintUIDToFirebase(int uid) {
    if (Firebase.ready()) {
        String path = "/fingerprint/registeredUIDs";
        if (Firebase.pushInt(fbData, path.c_str(), uid)) {
            Serial.print("Fingerprint UID ");
            Serial.print(uid);
            Serial.println(" added to Firebase successfully!");
        } else {
            Serial.print("Error adding UID to Firebase: ");
            Serial.println(fbData.errorReason());
        }
    } else {
        Serial.println("Firebase not ready!");
    }
}

void handleFingerprintError() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("FP Error");
    lcd.setCursor(0, 1);
    lcd.print("Press B for SOS");
    delay(2000);
}

void emergencyOpenDoor() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Emergency Open");
    openDoor();
}

void openDoor() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Opening Door...");
    Serial.println("Relay ON - Door Opening");

    digitalWrite(RELAY_PIN, HIGH);
    delay(4000);
    digitalWrite(RELAY_PIN, LOW);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Door Closed");
    Serial.println("Relay OFF - Door Closed");
    delay(1000);

    resetLCD();
}
void registerFingerprint() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Place Finger");

    int id = 1; // Assign the ID, can be incremented for new registrations
    while (true) {
        int result = finger.getImage();
        if (result == FINGERPRINT_OK) {
            lcd.setCursor(0, 1);
            lcd.print("Image taken");
            break;
        } else if (result == FINGERPRINT_NOFINGER) {
            lcd.setCursor(0, 1);
            lcd.print("Waiting...");
        } else if (result == FINGERPRINT_PACKETRECIEVEERR) {
            lcd.setCursor(0, 1);
            lcd.print("Comm error");
        } else if (result == FINGERPRINT_IMAGEFAIL) {
            lcd.setCursor(0, 1);
            lcd.print("Image fail");
        }
        delay(100);
    }

    // Convert the image to a template
    int result = finger.image2Tz(1);
    if (result != FINGERPRINT_OK) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Template fail");
        delay(2000);
        return;
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Remove Finger");
    delay(2000);

    // Wait for the finger to be removed
    while (finger.getImage() != FINGERPRINT_NOFINGER) {
        delay(100);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Place Again");

    // Wait for the finger to be placed again
    while (true) {
        result = finger.getImage();
        if (result == FINGERPRINT_OK) {
            lcd.setCursor(0, 1);
            lcd.print("Image taken");
            break;
        } else if (result == FINGERPRINT_NOFINGER) {
            lcd.setCursor(0, 1);
            lcd.print("Waiting...");
        } else if (result == FINGERPRINT_PACKETRECIEVEERR) {
            lcd.setCursor(0, 1);
            lcd.print("Comm error");
        } else if (result == FINGERPRINT_IMAGEFAIL) {
            lcd.setCursor(0, 1);
            lcd.print("Image fail");
        }
        delay(100);
    }

    // Convert the second image to a template
    result = finger.image2Tz(2);
    if (result != FINGERPRINT_OK) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Template fail");
        delay(2000);
        return;
    }

    // Create a model from the two templates
    result = finger.createModel();
    if (result != FINGERPRINT_OK) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Model fail");
        delay(2000);
        return;
    }

    // Save the model to the sensor's flash
    result = finger.storeModel(id);
    if (result == FINGERPRINT_OK) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Registered!");
        lcd.setCursor(0, 1);
        lcd.print("ID: ");
        lcd.print(id);
        Serial.print("Fingerprint registered with UID: ");
        Serial.println(id);

        // Send UID to Firebase
        addFingerprintUIDToFirebase(id);
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Save failed");
        delay(2000);
    }

    delay(2000);
}

void unlockWithFingerprint() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Scan Finger");

    unsigned long startTime = millis();
    while (millis() - startTime < 10000) {
        if (finger.getImage() == FINGERPRINT_OK && 
            finger.image2Tz() == FINGERPRINT_OK && 
            finger.fingerFastSearch() == FINGERPRINT_OK) {

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Access Granted");
            lcd.setCursor(0, 1);
            lcd.print("UID: ");
            lcd.print(finger.fingerID);

            Serial.print("Access granted for UID: ");
            Serial.println(finger.fingerID);

            sendFingerprintUIDToFirebase(finger.fingerID);
            openDoor();
            return;
        }
        delay(2000);
                
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Access Denied");
    delay(2000);
}
