#include <CustomStepper.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <FS.h>

int switchPin = 4;

// Config variables
char domain[100];
char host[100];
char path[200];
char ssid[32];
char pw[63];


// AP config
bool apMode = false;
const char *apSsid = "clock";
ESP8266WebServer server( 80 );

// Working variables
bool zeroing = false;

CustomStepper stepper( 14, 12, 13, 15 );
WiFiClientSecure client;

/**
 * Load a string from a file and load its contents into the given variable 
 */
void getFileString( char const fileName[], char* var, int charSize ) {
  File file = SPIFFS.open( fileName, "r" );
  if ( ! file ) {
    Serial.print( "File open for read failed " );
    Serial.println( fileName );
  }

  String string = file.readString();
  var[0] = (char) 0;
  file.seek( 0, SeekSet );
  string.toCharArray( var, charSize );

  file.close();
}

/**
 * Save a string to the given file 
 */
void setFileString( char const fileName[], String value ) {
  File file = SPIFFS.open( fileName, "w" );
  if ( ! file ) {
    Serial.print( "file open for write failed " );
    Serial.println( fileName );
  }

  value.trim();

  // @see: https://github.com/esp8266/Arduino/issues/454
  value.replace("+", " ");
  value.replace("%21", "!");
  value.replace("%23", "#");
  value.replace("%24", "$");
  value.replace("%26", "&");
  value.replace("%27", "'");
  value.replace("%28", "(");
  value.replace("%29", ")");
  value.replace("%2A", "*");
  value.replace("%2B", "+");
  value.replace("%2C", ",");
  value.replace("%2F", "/");
  value.replace("%3A", ":");
  value.replace("%3B", ";");
  value.replace("%3D", "=");
  value.replace("%3F", "?");
  value.replace("%40", "@");
  value.replace("%5B", "[");
  value.replace("%5D", "]");

  file.print( value );

  file.close();
}

/**
 * Load all variables 
 */
void populateVars() {
  getFileString( "/ssid", ssid, sizeof( ssid ) );
  getFileString( "/pw", pw, sizeof( pw ) );
  getFileString( "/domain", domain, sizeof( domain ) );
  getFileString( "/host", host, sizeof( host ) );
  getFileString( "/path", path, sizeof( path ) );
}

void handleRoot() {
 if ( HTTP_POST == server.method() ) {
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      if ( server.argName( i ).equals( "ssid" )  ) {
        setFileString( "/ssid", server.arg( i ) );
      } else if ( server.argName( i ).equals( "pw" ) ) {
        setFileString( "/pw", server.arg( i ) );
      } else if ( server.argName( i ).equals( "domain" ) ) {
        setFileString( "/domain", server.arg( i ) );
      } else if ( server.argName( i ).equals( "host" ) ) {
        setFileString( "/host", server.arg( i ) );
      } else if ( server.argName( i ).equals( "path" ) ) {
        setFileString( "/path", server.arg( i ) );
      }
    }
  }

  // Load variables from storage
  populateVars();

  String html = "<html><body>";
  html += "<h1>Settings</h1>";
  html += "<form method='POST'><table>";
  html += "<tr><td>SSID<td><td><input name='ssid' type='text' size='32' value='%ssid%'></td></tr>";
  html += "<tr><td>Passphrase<td><td><input name='pw' type='password' size='63'></td></tr>";
  html += "<tr><td>IP/Domain<td><td><input name='domain' type='text' value='%domain%'></td></tr>";
  html += "<tr><td>Hostname<td><td><input name='host' type='text' value='%host%'></td></tr>";
  html += "<tr><td>Path<td><td><input name='path' type='text' value='%path%'></td></tr>";
  html += "<tr><td><td><td><input type='submit' value='Save'></td></tr>";
  html += "</table></form>";
  html += "</body></html>";

  html.replace( "%ssid%", ssid );
  html.replace( "%domain%", domain );
  html.replace( "%host%", host );
  html.replace( "%path%", path );

  server.send( 200, "text/html", html );
}

/**
 * Starts access point mode to allow settings to be changed
 * */
void startAP() {
  WiFi.disconnect();

  WiFi.mode( WIFI_AP );
  WiFi.softAP( apSsid );
  Serial.println( "" );
  Serial.println( "Started AP with IP " );
  Serial.println( WiFi.softAPIP() );

  server.on( "/", handleRoot );
  server.begin();
}

String htmlspecialcharts( String input ) {
  input.replace( "<", "&lt;" );
  input.replace( ">", "&gt" );
  input.replace( "\"", "&quot;" );
  input.replace( "&", "&amp;" );
  return input;
}

void setPosition( int deg ) {
  // Check for out of range values
  if ( deg < 0 || deg > 359 ) {
    Serial.print( "Invalid position: " );
    Serial.println( deg );
    return;
  }

  Serial.println( "" );

  Serial.print( "Set to: " );
  Serial.println( deg );

  File posFile;

  posFile = SPIFFS.open( "/pos", "r" );

  int currentPos = posFile.readString().toInt();

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

  posFile = SPIFFS.open( "/pos", "w" );
  posFile.print( deg );

  Serial.print( "Rotating: " );
  Serial.println( rotate );
}

/**
 * Connect to the configured WIFI
 * */
void connectToWifi() {

  Serial.println();
  Serial.println();
  Serial.print( F( "Connecting to " ) );
  Serial.println ( ssid );

  WiFi.begin( ssid, pw );

  int tries = 0;
  while ( WiFi.status() != WL_CONNECTED ) {
    delay( 500 );

    Serial.print( F( "." ) );

    tries ++;
    if ( 40 < tries  ) {
      // Reboot if connecting takes to long;
      ESP.deepSleep( 2500000 );
    }
  }

  Serial.println();
  Serial.println( F( "WiFi connected" ) );
  Serial.print( F( "IP address: " ) );
  Serial.println( WiFi.localIP() );
}

void getFromServer() {
  Serial.println();
  Serial.print( F( "connecting to " ) );
  Serial.println( domain );
  
  // WiFiClientSecure client;
   if ( ! client.connect( domain, 443 ) ) {
    Serial.println( F( "connection failed" ) );
    return;
  }

  // We now create a URI for the request
  String url = String( "https://" );
  url.concat( host );
  url.concat( path );

  Serial.print( "Requesting URL: " );
  Serial.println( url );

  // This will send the request to the server
  client.print( String( "GET " ) + url + " HTTP/1.1\r\n" +
                "Host: " + host + "\r\n" +
                "Connection: close\r\n\r\n");
  
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
  client.readStringUntil( ':' );
  
   // Skip first line of response;
  String line = client.readStringUntil( '}' );
    
  setPosition( line.toInt() );
}

void setup() {
  Serial.begin( 115200 );

  Serial.println( ESP.getFreeHeap() );

  pinMode( switchPin, INPUT_PULLUP );

  if ( LOW == digitalRead( switchPin ) ) {
    apMode = true;
    // Allow user to release button
    delay( 2000 );
  }

  stepper.setRPM( 3 );
  stepper.setDirection( CW );

  // Start file system
  SPIFFS.begin();
  //SPIFFS.format();

  if ( apMode ) {
    startAP();
    return;
  }

   // Load variables
  populateVars();

  // Connect
  connectToWifi();

  // Load data from server and set position
  getFromServer();
}

void loop() {
  // Perform stepper motor actions
  stepper.run();

  // Switch off motor once everything is done to reduce power consumption
  if ( stepper.isDone() ) {
    digitalWrite( 14, LOW );
    digitalWrite( 13, LOW );
    digitalWrite( 12, LOW );
    digitalWrite( 15, LOW );
  }

  // Enable web server and zeroing in AP mode
  if ( apMode ) {
    if ( LOW == digitalRead( switchPin ) && false == zeroing ) {
      Serial.println("start");
      delay( 100 );

      stepper.setRPM( 1 );
      stepper.setDirection( CW );
      stepper.rotate();
      zeroing = true;
      return;
    }

    if ( HIGH == digitalRead( switchPin ) && true == zeroing ) {
      Serial.println("stop");

      stepper.setRPM( 3 );
      stepper.setDirection( STOP );
      zeroing = false;

      File posFile = SPIFFS.open( "/pos", "w" );
      posFile.print( 0 );
      return;
    }

    server.handleClient();
    return;
  }

  // Enter sleep mode once everything is done
  if ( stepper.isDone() ) {
    Serial.println();
    Serial.println( "closing WIFI connection" );
    WiFi.disconnect();
    delay( 1000 );
    ESP.deepSleep( 600 * 1000000 );
  }
}
