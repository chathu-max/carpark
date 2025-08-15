#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>

// Wi-Fi credentials - CHANGE THESE TO YOUR NETWORK
const char* ssid = "Chathuboy";     // Replace with your WiFi Name
const char* password = "00000000";  // Replace with your WiFi Password

// Firebase credentials
#define API_KEY "AIzaSyDTwybJzvUae23kME5yfPjDDEtYb-pPNNQ"
#define DATABASE_URL "https://carparkwebb-default-rtdb.firebaseio.com"
#define USER_EMAIL "webapp@gmail.com"
#define USER_PASSWORD "200729300628"

// Entry gate hardware pins
#define ENTRY_TRIG_PIN 12
#define ENTRY_ECHO_PIN 13
#define ENTRY_SERVO_PIN 19
#define ENTRY_SENSOR_LED 23

// Exit gate hardware pins
#define EXIT_TRIG_PIN 14
#define EXIT_ECHO_PIN 15
#define EXIT_SERVO_PIN 18
#define EXIT_SENSOR_LED 22

// System constants
#define DETECTION_THRESHOLD 9  // cm - distance to detect vehicle
#define SERVO_OPEN_ANGLE 90    // degrees
#define SERVO_CLOSE_ANGLE 0    // degrees
#define GATE_OPEN_DELAY 3000   // milliseconds

// Servo objects
Servo entryGateServo;
Servo exitGateServo;

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// System variables
int totalCount = 6;    // Total parking spaces
int currentCount = 0;  // Current occupied spaces
bool systemReady = false;

// Function prototypes
long getDistanceCM(int trigPin, int echoPin);
void openGate(Servo& servo, int ledPin, String gateName);
void closeGate(Servo& servo, int ledPin, String gateName);
void updateFirebase();
bool initializeFirebase();
void connectToWiFi();

void setup() {
  Serial.begin(115200);
  Serial.println("\n🚗 ESP32 Car Park System Starting...");

  // Initialize hardware pins
  // Entry sensor pins
  pinMode(ENTRY_TRIG_PIN, OUTPUT);
  pinMode(ENTRY_ECHO_PIN, INPUT);
  pinMode(ENTRY_SENSOR_LED, OUTPUT);
  
  // Exit sensor pins
  pinMode(EXIT_TRIG_PIN, OUTPUT);
  pinMode(EXIT_ECHO_PIN, INPUT);
  pinMode(EXIT_SENSOR_LED, OUTPUT);

  // Initialize servos
  entryGateServo.attach(ENTRY_SERVO_PIN);
  exitGateServo.attach(EXIT_SERVO_PIN);
  
  // Close gates initially
  entryGateServo.write(SERVO_CLOSE_ANGLE);
  exitGateServo.write(SERVO_CLOSE_ANGLE);
  
  // Turn off LEDs initially
  digitalWrite(ENTRY_SENSOR_LED, LOW);
  digitalWrite(EXIT_SENSOR_LED, LOW);

  // Connect to Wi-Fi
  connectToWiFi();
  
  // Initialize Firebase
  if (initializeFirebase()) {
    systemReady = true;
    Serial.println("✅ System ready! Monitoring for vehicles...");
  } else {
    Serial.println("❌ System initialization failed!");
  }
}

void loop() {
  if (!systemReady) {
    Serial.println("⚠️ System not ready, retrying initialization...");
    if (initializeFirebase()) {
      systemReady = true;
    }
    delay(5000);
    return;
  }

  // Check entry sensor
  long entryDistance = getDistanceCM(ENTRY_TRIG_PIN, ENTRY_ECHO_PIN);
  
  if (entryDistance > 0 && entryDistance < DETECTION_THRESHOLD) {
    Serial.print("🚗 Vehicle detected at entry! Distance: ");
    Serial.print(entryDistance);
    Serial.println(" cm");
    
    // Get current counts from Firebase
    if (Firebase.RTDB.getInt(&fbdo, "/total_count")) {
      totalCount = fbdo.intData();
    }
    
    if (Firebase.RTDB.getInt(&fbdo, "/current_count")) {
      currentCount = fbdo.intData();
    }
    
    Serial.print("📊 Current: ");
    Serial.print(currentCount);
    Serial.print("/");
    Serial.println(totalCount);
    
    // Check if space is available
    if (currentCount < totalCount) {
      Serial.println("✅ Space available! Opening entry gate...");
      digitalWrite(ENTRY_SENSOR_LED, HIGH);
      
      // Open entry gate
      openGate(entryGateServo, ENTRY_SENSOR_LED, "Entry");
      
      // Update count
      currentCount++;
      updateFirebase();
      
      Serial.print("📡 Updated count to: ");
      Serial.println(currentCount);
      
      // Keep gate open for specified time
      delay(GATE_OPEN_DELAY);
      
      // Close entry gate
      closeGate(entryGateServo, ENTRY_SENSOR_LED, "Entry");
      
      // Wait for vehicle to pass completely
      delay(2000);
    } else {
      Serial.println("❌ Parking Full! Entry denied.");
      // Blink LED to indicate full
      for (int i = 0; i < 6; i++) {
        digitalWrite(ENTRY_SENSOR_LED, HIGH);
        delay(200);
        digitalWrite(ENTRY_SENSOR_LED, LOW);
        delay(200);
      }
    }
  }

  // Check exit sensor
  long exitDistance = getDistanceCM(EXIT_TRIG_PIN, EXIT_ECHO_PIN);
  
  if (exitDistance > 0 && exitDistance < DETECTION_THRESHOLD) {
    Serial.print("🚗 Vehicle detected at exit! Distance: ");
    Serial.print(exitDistance);
    Serial.println(" cm");
    
    // Get current count from Firebase
    if (Firebase.RTDB.getInt(&fbdo, "/current_count")) {
      currentCount = fbdo.intData();
    }
    
    if (currentCount > 0) {
      Serial.println("🚪 Vehicle exiting! Opening exit gate...");
      digitalWrite(EXIT_SENSOR_LED, HIGH);
      
      // Open exit gate
      openGate(exitGateServo, EXIT_SENSOR_LED, "Exit");
      
      // Update count
      currentCount--;
      updateFirebase();
      
      Serial.print("📡 Updated count to: ");
      Serial.println(currentCount);
      
      // Keep gate open for specified time
      delay(GATE_OPEN_DELAY);
      
      // Close exit gate
      closeGate(exitGateServo, EXIT_SENSOR_LED, "Exit");
      
      // Wait for vehicle to pass completely
      delay(2000);
    } else {
      Serial.println("⚠️ No vehicles to exit!");
      // Brief LED flash
      digitalWrite(EXIT_SENSOR_LED, HIGH);
      delay(500);
      digitalWrite(EXIT_SENSOR_LED, LOW);
    }
  }

  // Small delay to prevent sensor spam
  delay(500);
  
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("🔄 WiFi disconnected, reconnecting...");
    connectToWiFi();
  }
}

// Function to measure distance using ultrasonic sensor
long getDistanceCM(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout
  
  if (duration == 0) {
    return -1; // No echo received
  }
  
  long distance = duration * 0.034 / 2;
  return distance;
}

// Function to open gate
void openGate(Servo& servo, int ledPin, String gateName) {
  Serial.println("🔓 Opening " + gateName + " gate...");
  servo.write(SERVO_OPEN_ANGLE);
  digitalWrite(ledPin, HIGH);
}

// Function to close gate
void closeGate(Servo& servo, int ledPin, String gateName) {
  Serial.println("🔒 Closing " + gateName + " gate...");
  servo.write(SERVO_CLOSE_ANGLE);
  digitalWrite(ledPin, LOW);
}

// Function to update Firebase with current count
void updateFirebase() {
  if (Firebase.RTDB.setInt(&fbdo, "/current_count", currentCount)) {
    Serial.println("✅ Firebase updated successfully");
  } else {
    Serial.println("❌ Firebase update failed: " + fbdo.errorReason());
  }
}

// Function to connect to WiFi
void connectToWiFi() {
  Serial.print("🌐 Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi!");
    Serial.print("📡 IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi connection failed!");
  }
}

// Function to initialize Firebase
bool initializeFirebase() {
  Serial.println("🔧 Initializing Firebase...");
  
  // Configure Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  
  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Test Firebase connection
  if (Firebase.RTDB.getInt(&fbdo, "/current_count")) {
    currentCount = fbdo.intData();
    Serial.println("✅ Firebase connected successfully!");
    Serial.print("📊 Current vehicle count: ");
    Serial.println(currentCount);
    return true;
  } else {
    Serial.println("❌ Firebase connection failed: " + fbdo.errorReason());
    return false;
  }
}

// Function to handle system reset (can be called via Firebase or button)
void resetSystem() {
  Serial.println("🔄 Resetting system...");
  
  currentCount = 0;
  updateFirebase();
  
  // Close both gates
  entryGateServo.write(SERVO_CLOSE_ANGLE);
  exitGateServo.write(SERVO_CLOSE_ANGLE);
  
  // Turn off LEDs
  digitalWrite(ENTRY_SENSOR_LED, LOW);
  digitalWrite(EXIT_SENSOR_LED, LOW);
  
  Serial.println("✅ System reset complete!");
}

// Function for emergency gate opening (can be triggered via Firebase)
void emergencyOpen() {
  Serial.println("🚨 EMERGENCY: Opening all gates!");
  
  entryGateServo.write(SERVO_OPEN_ANGLE);
  exitGateServo.write(SERVO_OPEN_ANGLE);
  
  digitalWrite(ENTRY_SENSOR_LED, HIGH);
  digitalWrite(EXIT_SENSOR_LED, HIGH);
  
  // Keep gates open until manual reset
  Serial.println("⚠️ Gates will remain open until system reset");
}

// Optional: Function to check for remote commands from Firebase
void checkRemoteCommands() {
  if (Firebase.RTDB.getString(&fbdo, "/command")) {
    String command = fbdo.stringData();
    
    if (command == "reset") {
      resetSystem();
      Firebase.RTDB.setString(&fbdo, "/command", ""); // Clear command
    } else if (command == "emergency") {
      emergencyOpen();
      Firebase.RTDB.setString(&fbdo, "/command", ""); // Clear command
    }
  }
}