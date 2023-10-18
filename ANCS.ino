// Original: https://github.com/S-March/esp32_ANCS
// fixed for Arduino15/packages/esp32/hardware/esp32/1.0.3

#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEClient.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include <esp_log.h>
#include <esp_bt_main.h>
#include <string>
#include "Task.h"
#include <sys/time.h>
#include <time.h>
#include "sdkconfig.h"

static char LOG_TAG[] = "SampleServer";

static BLEUUID ancsServiceUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
static BLEUUID notificationSourceCharacteristicUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD");
static BLEUUID controlPointCharacteristicUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
static BLEUUID dataSourceCharacteristicUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");


uint8_t latestMessageID[4];
boolean pendingNotification = false;
boolean incomingCall = false;
uint8_t acceptCall = 0;

class MySecurity : public BLESecurityCallbacks {

    uint32_t onPassKeyRequest(){
        ESP_LOGI(LOG_TAG, "PassKeyRequest");
        return 123456;
    }

    void onPassKeyNotify(uint32_t pass_key){
        ESP_LOGI(LOG_TAG, "On passkey Notify number:%d", pass_key);
    }

    bool onSecurityRequest(){
        ESP_LOGI(LOG_TAG, "On Security Request");
        return true;
    }
    
    bool onConfirmPIN(unsigned int){
        ESP_LOGI(LOG_TAG, "On Confrimed Pin Request");
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl){
        ESP_LOGI(LOG_TAG, "Starting BLE work!");
        if(cmpl.success){
            uint16_t length;
            esp_ble_gap_get_whitelist_size(&length);
            ESP_LOGD(LOG_TAG, "size: %d", length);
        }
    }
};

static void dataSourceNotifyCallback(
  BLERemoteCharacteristic* pDataSourceCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    //Serial.print("Notify callback for characteristic ");
    //Serial.print(pDataSourceCharacteristic->getUUID().toString().c_str());
    //Serial.print(" of data length ");
    //Serial.println(length);
    for(int i = 0; i < length; i++){
        if(i > 7){
            Serial.write(pData[i]);
        }
        /*else{
            Serial.print(pData[i], HEX);
            Serial.print(" ");
        }*/
    }
    Serial.println();
}

static void NotificationSourceNotifyCallback(
  BLERemoteCharacteristic* pNotificationSourceCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify)
{
    if(pData[0]==0)
    {
        Serial.println("New notification!");
        //Serial.println(pNotificationSourceCharacteristic->getUUID().toString().c_str());
        latestMessageID[0] = pData[4];
        latestMessageID[1] = pData[5];
        latestMessageID[2] = pData[6];
        latestMessageID[3] = pData[7];
        
        switch(pData[2])
        {
            case 0:
                Serial.println("Category: Other");
            break;
            case 1:
                incomingCall = true;
                Serial.println("Category: Incoming call");
            break;
            case 2:
                Serial.println("Category: Missed call");
            break;
            case 3:
                Serial.println("Category: Voicemail");
            break;
            case 4:
                Serial.println("Category: Social");
            break;
            case 5:
                Serial.println("Category: Schedule");
            break;
            case 6:
                Serial.println("Category: Email");
            break;
            case 7:
                Serial.println("Category: News");
            break;
            case 8:
                Serial.println("Category: Health");
            break;
            case 9:
                Serial.println("Category: Business");
            break;
            case 10:
                Serial.println("Category: Location");
            break;
            case 11:
                Serial.println("Category: Entertainment");
            break;
            default:
            break;
        }
    }
    else if(pData[0]==1)
    {
      Serial.println("Notification Modified!");
      if (pData[2] == 1) {
        Serial.println("Call Changed!");
      }
    }
    else if(pData[0]==2)
    {
      Serial.println("Notification Removed!");
      if (pData[2] == 1) {
        Serial.println("Call Gone!");
      }
    }
    //Serial.println("pendingNotification");
    pendingNotification = true;
}


/**
 * Become a BLE client to a remote BLE server.  We are passed in the address of the BLE server
 * as the input parameter when the task is created.
 */
class MyClient: public Task {
    void run(void* data) {

        BLEAddress* pAddress = (BLEAddress*)data;
        BLEClient*  pClient  = BLEDevice::createClient();
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_IO);
        pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        // Connect to the remove BLE Server.
        pClient->connect(*pAddress);

        /** BEGIN ANCS SERVICE **/
        // Obtain a reference to the service we are after in the remote BLE server.
        BLERemoteService* pAncsService = pClient->getService(ancsServiceUUID);
        if (pAncsService == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our service UUID: %s", ancsServiceUUID.toString().c_str());
            return;
        }
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pNotificationSourceCharacteristic = pAncsService->getCharacteristic(notificationSourceCharacteristicUUID);
        if (pNotificationSourceCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", notificationSourceCharacteristicUUID.toString().c_str());
            return;
        }        
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pControlPointCharacteristic = pAncsService->getCharacteristic(controlPointCharacteristicUUID);
        if (pControlPointCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", controlPointCharacteristicUUID.toString().c_str());
            return;
        }        
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        BLERemoteCharacteristic* pDataSourceCharacteristic = pAncsService->getCharacteristic(dataSourceCharacteristicUUID);
        if (pDataSourceCharacteristic == nullptr) {
            ESP_LOGD(LOG_TAG, "Failed to find our characteristic UUID: %s", dataSourceCharacteristicUUID.toString().c_str());
            return;
        }        
        const uint8_t v[]={0x1,0x0};
        pDataSourceCharacteristic->registerForNotify(dataSourceNotifyCallback);
        pDataSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)v,2,true);
        pNotificationSourceCharacteristic->registerForNotify(NotificationSourceNotifyCallback);
        pNotificationSourceCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)v,2,true);
        /** END ANCS SERVICE **/







      while(1){
        //Serial.print("running ");
        //Serial.println(pendingNotification);
            if(pendingNotification || incomingCall){
                // CommandID: CommandIDGetNotificationAttributes
                // 32bit uid
                // AttributeID
                Serial.println("Requesting details...");
                const uint8_t vIdentifier[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x0};
                pControlPointCharacteristic->writeValue((uint8_t*)vIdentifier,6,true);
                const uint8_t vTitle[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x1, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t*)vTitle,8,true);
                const uint8_t vMessage[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x3, 0x0, 0x10};
                pControlPointCharacteristic->writeValue((uint8_t*)vMessage,8,true);
                const uint8_t vDate[]={0x0,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x5};
                pControlPointCharacteristic->writeValue((uint8_t*)vDate,6,true);


                while (incomingCall)
                {
                  if (Serial.available() > 0) {
                    acceptCall = Serial.read();
                    Serial.println((char)acceptCall);
                  }
                  
                  if (acceptCall == 49) { //call accepted , get number 1 from serial
                    const uint8_t vResponse[]={0x02,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x00};
                    pControlPointCharacteristic->writeValue((uint8_t*)vResponse,6,true);

                    acceptCall = 0;
                    //incomingCall = false;
                  }
                  else if (acceptCall == 48) {  //call rejected , get number 0 from serial
                    const uint8_t vResponse[]={0x02,   latestMessageID[0],latestMessageID[1],latestMessageID[2],latestMessageID[3],   0x01};
                    pControlPointCharacteristic->writeValue((uint8_t*)vResponse,6,true);

                    acceptCall = 0;
                    incomingCall = false;
                  }
                }
               
                               
                pendingNotification = false;
            }
            delay(100); //does not work without small delay
        }








        
    } // run
}; // MyClient

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
        Serial.println("********************");
        Serial.println("**Device connected**");
        Serial.println(BLEAddress(param->connect.remote_bda).toString().c_str());
        Serial.println("********************");
        MyClient* pMyClient = new MyClient();
        pMyClient->setStackSize(18000);
        pMyClient->start(new BLEAddress(param->connect.remote_bda));
    };

    void onDisconnect(BLEServer* pServer) {
        Serial.println("************************");
        Serial.println("**Device  disconnected**");
        Serial.println("************************");
    }
};

class MainBLEServer: public Task {
    void run(void *data) {
        ESP_LOGD(LOG_TAG, "Starting BLE work!");
        esp_log_buffer_char(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));
        esp_log_buffer_hex(LOG_TAG, LOG_TAG, sizeof(LOG_TAG));

        // Initialize device
        BLEDevice::init("ANCS");
        BLEServer* pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
        BLEDevice::setSecurityCallbacks(new MySecurity());

        // Advertising parameters:
        // Soliciting ANCS
        BLEAdvertising *pAdvertising = pServer->getAdvertising();
        BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
        oAdvertisementData.setFlags(0x01);
        _setServiceSolicitation(&oAdvertisementData, BLEUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0"));
        pAdvertising->setAdvertisementData(oAdvertisementData);        

        // Set security
        BLESecurity *pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        pSecurity->setCapability(ESP_IO_CAP_OUT);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

        //Start advertising
        pAdvertising->start();
        
        ESP_LOGD(LOG_TAG, "Advertising started!");
        delay(portMAX_DELAY);
    }

    
    /**
     * @brief Set the service solicitation (UUID)
     * @param [in] uuid The UUID to set with the service solicitation data.  Size of UUID will be used.
     */
    void _setServiceSolicitation(BLEAdvertisementData *a, BLEUUID uuid)
    {
      char cdata[2];
      switch(uuid.bitSize()) {
        case 16: {
          // [Len] [0x14] [UUID16] data
          cdata[0] = 3;
          cdata[1] = ESP_BLE_AD_TYPE_SOL_SRV_UUID;  // 0x14
          a->addData(std::string(cdata, 2) + std::string((char *)&uuid.getNative()->uuid.uuid16,2));
          break;
        }
    
        case 128: {
          // [Len] [0x15] [UUID128] data
          cdata[0] = 17;
          cdata[1] = ESP_BLE_AD_TYPE_128SOL_SRV_UUID;  // 0x15
          a->addData(std::string(cdata, 2) + std::string((char *)uuid.getNative()->uuid.uuid128,16));
          break;
        }
    
        default:
          return;
      }
    } // setServiceSolicitationData

    
};

void SampleSecureServer(void)
{
    MainBLEServer* pMainBleServer = new MainBLEServer();
    pMainBleServer->setStackSize(20000);
    pMainBleServer->start();
}

void setup()
{
    Serial.begin(115200);
    SampleSecureServer();
}
void loop()
{
}
