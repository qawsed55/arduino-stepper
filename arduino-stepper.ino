#include <CustomStepper.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <DoubleResetDetector.h>

// Number of seconds after reset during which a
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 3

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

#define SENSOR_PWR_PIN 4
#define SENSOR_IN_PIN 5
#define SWITCH_PIN = 5;

struct {
  int check;
  int pos;
} currentPosition;

// Config variables
char domain[100];
char path[200];

// Working variables
bool shouldSaveConfig = false;
bool zeroingMode = false;
int targetPosition = 0;

CustomStepper stepper( 14, 12, 13, 15 );
WiFiClientSecure client;

/**
 * Commands to the indicator to move to the zero position
 */
void setToZero() {
  Serial.println( "setting to 0" );

  pinMode( SENSOR_PWR_PIN, OUTPUT );
  pinMode( SENSOR_IN_PIN, INPUT );
  Serial.print( "setting to 0" );
  delay(1000);
  
  digitalWrite( SENSOR_PWR_PIN, HIGH );
  delay(1000);

  zeroingMode = true;

  stepper.setDirection( CW );
  stepper.rotate();
}

/**
 * Returns the current position of the indicator. If the stored position is invalid the return will be -1 
 */
int getCurrentPosition() {
  if ( ! ESP.rtcUserMemoryRead( 4, ( uint32_t* ) &currentPosition, sizeof( currentPosition ) )
       || 359 != ( currentPosition.pos + currentPosition.check ) )  {

    return -1;
  }
 
  return currentPosition.pos;
}

/**
 * Saves the given position to RTC memory.
 */
void saveCurrentPosition( int position ) {
  currentPosition.pos = position;
  currentPosition.check = 359 - position;
  Serial.print( "setting to " );
  Serial.println( currentPosition.check );
  ESP.rtcUserMemoryWrite( 4, ( uint32_t* ) &currentPosition, sizeof( currentPosition )  );
}

/**
 * Set the indicator to the given position
 */
void setPosition( int deg, int currentPosition ) {
  // Check for out of range values
  if ( deg < 0 || deg > 359 ) {
    Serial.print( "Invalid position: " );
    Serial.println( deg );
    return;
  }

  Serial.println( "" );

  Serial.print( "Set to: " );
  Serial.println( deg );

  int currentPos = getCurrentPosition();

  Serial.print( "Current: " );
  Serial.println( currentPos );

  // Do nothing if no rotation is needed
  if ( currentPos == deg ) {
    Serial.println( "No rotation needed" );
    return;
  }

  int rotate = 0;
  if ( currentPos <= deg ) {
    rotate = deg - currentPos;
  } else {
    rotate = 359 - ( currentPos - deg );
  }

  if ( rotate < 180 ) {
    stepper.setDirection( CW );
  } else {
    stepper.setDirection( CCW );
    rotate = 359 - rotate;
  }

  stepper.rotateDegrees( rotate );
  Serial.print( "Rotating: " );
  Serial.println( rotate );
}

/**
 * Fetches the target position from the configured web server 
 */
int getFromServer() {
  Serial.println();
  Serial.print( F( "connecting to " ) );
  Serial.println( domain );

  // WiFiClientSecure client;
  if ( ! client.connect( domain, 443 ) ) {
    Serial.println( F( "connection failed" ) );
    return - 1;
  }

  // We now create a URI for the request
  String url = String( "https://" );
  url.concat( domain );
  url.concat( path );

  Serial.print( "Requesting URL: " );
  Serial.println( url );

  // This will send the request to the server
  client.print( String( "GET " ) + url + " HTTP/1.1\r\n" +
                "Host: " + domain + "\r\n" +
                "Connection: close\r\n\r\n" );

  delay( 5000 );

  // Needed so available() actually works
  client.readStringUntil( '\n' );
  while ( client.available() ) {
    String line = client.readStringUntil( '\n' );
    if ( line == "\r" ) {
      Serial.println( "headers received" );
      break;
    }
  }

  // Response format => {position: 359}
  String l2 = client.readStringUntil( ':' );

  // Skip first line of response;
  String line = client.readStringUntil( '}' );

  Serial.print( "Server said move to " );
  Serial.println( line );

  return line.toInt();
}

/**
 * Loads the configuration from SPIFFS
 */
void loadConfig() {
  if ( ! SPIFFS.begin() ) {
    Serial.println( "failed to mount FS" );
    return;
  }

  Serial.println( "mounted file system" );

  if ( ! SPIFFS.exists( "/config.json" ) ) {
    Serial.println( "config.json does not exist" );
    return;
  }

  File configFile = SPIFFS.open( "/config.json", "r" );
  if ( ! configFile ) {
    Serial.println( "config.json could not be loaded" );
    return;
  }

  Serial.println( "opened config file" );
  size_t size = configFile.size();

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());
  json.printTo(Serial);
  if ( ! json.success() ) {
    Serial.println( "config.json is invalid" );
    return;
  }

  Serial.println("\nparsed json");

  strcpy( domain, json["domain"] );
  strcpy( path, json["path"] );
}

/**
 * Saves the configuration
 */ 
void saveConfig() {
  Serial.println( "Saving configuration" );
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  json["domain"] = domain;
  json["path"] = path;

  File configFile = SPIFFS.open( "/config.json", "w" );
  if ( ! configFile ) {
    Serial.println( "Failed to open config file for writing" );
  }

  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
}

/**
 * Callback function so set shouldSaveConfig variable
 */
void saveConfigCallback () {
  Serial.println( "Should save config" );
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin( 115200 );

  stepper.setRPM( 3 );
  stepper.setDirection( CW );

  loadConfig();

  WiFiManagerParameter custom_domain( "domain", "Domain name e.g. www.kjero.com", domain, 100 );
  WiFiManagerParameter custom_path( "path", "Path. e.g /wp-admin/admin-ajax.php?action=stepper&key=YOURKEY", path, 200);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback( saveConfigCallback );

  wifiManager.setDebugOutput(true);

  wifiManager.addParameter( &custom_domain );
  wifiManager.addParameter( &custom_path );

  if ( drd.detectDoubleReset() ) {
    wifiManager.startConfigPortal( "Clock display", "kjero_clock" );
    ESP.reset();
  } else if ( ! wifiManager.autoConnect( "Clock display", "kjero_clock" ) ) {
   
    Serial.println( "failed to connect and hit timeout" );
   
    delay(5000);
    ESP.deepSleep( 600 * 1000000 );
  }

  if ( shouldSaveConfig ) {
    strcpy(domain, custom_domain.getValue());
    strcpy(path, custom_path.getValue());
    saveConfig();
  }

  //pinMode( switchPin, INPUT_PULLUP );

  targetPosition = getFromServer();
  int currentPosition = getCurrentPosition();

  Serial.print( "Current position:" );
  Serial.println( currentPosition );

  if ( -1 == currentPosition ) {
    setToZero();
  } else {
    setPosition( targetPosition, currentPosition );
  }

}

void loop() {
  // Perform stepper motor actions
  stepper.run();

  // Enable web server and zeroing in AP mode
  if ( true == zeroingMode && 100 > analogRead( A0 ) ) {
    zeroingMode = false;
    stepper.setDirection( STOP );
    digitalWrite( SENSOR_PWR_PIN, LOW );

    Serial.println( "Zeroing complete" );

    saveCurrentPosition(0);
    setPosition( targetPosition, 0 );
  }

  // Enter sleep mode once everything is done
  if ( stepper.isDone() ) {
    saveCurrentPosition(targetPosition);
    // Switch off motor once everything is done to reduce power consumption
    digitalWrite( 14, LOW );
    digitalWrite( 13, LOW );
    digitalWrite( 12, LOW );
    digitalWrite( 15, LOW );
    Serial.println();
    Serial.println( "Going to sleep" );
    delay( 1000 );
    ESP.deepSleep( 600 * 1000000, WAKE_RF_DEFAULT );
  }
}
