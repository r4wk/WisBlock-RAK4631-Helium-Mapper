/**
 * @file fieldtester.cpp
 * @author r4wk (r4wknet@gmail.com)
 * @brief RAK FieldTester/Mapper
 * @version 0.2a
 * @date 2022-02-15
 * 
 * @copyright Copyright (c) 2022
 * TODO: Detect display so can still work without working/connected display
 *       Use proper icons?
 *       Add more to MYLOG()
 *       Add RAK12500_GNSS compatibility
 *       Kill some logic when display is off
 * 
 */

#include <app.h>

// Vector string array for display. MAX 9 lines Y. MAX 32 characters X
std::vector<std::string> displayBuffer;
// Keep track of beacons
int32_t txCount = 0;
int32_t rxCount = 0;
// Battery level
int8_t battLevel = 0;
// Is display is on/off
bool displayOn = true;
// Timer to put display to sleep, mostly to save burn in
SoftwareTimer displayTimeoutTimer;
// Timer to update battery level
SoftwareTimer battTimer;
// Field tester lat/long
double ftester_lat = 0.0;
double ftester_long = 0.0;
// GPS sat count/fix status
int8_t ftester_satCount = 0;
bool ftester_gpsLock = false;
// Is field tester busy
bool ftester_busy = false;

// Instance for display object (RENDER UPSIDE DOWN)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2);

/**
 * @brief Redraw info bar and display buffer with up to date info
 * TODO: Space out icons better
 * 
 * Firmware version
 * GPS fix status
 * Helium network status
 * Beacon RX/TX Count
 * Battery level indicator
 * Display buffer
 * 
 */
void refreshDisplay(void)
{
    if(displayOn)
    {
        // Clear screen
        u8g2.clearBuffer();

        // Draw firmware version
        u8g2.drawStr(0, 5, "R4K v0.3a");

        // Draw GPS sat fix count
        u8g2.drawStr(38, 5, "(GPS)");
        u8g2.drawStr(58, 5, std::to_string(ftester_satCount).c_str());

        // Draw Helium Join Status
        // RX/TX Count
        if(g_join_result) 
        {
            u8g2.drawStr(68, 5, "(H)");
            std::string count = std::to_string(rxCount) + "/" + std::to_string(txCount);
            u8g2.drawStr(80, 5,  count.c_str());
        }

        // Draw battery level based on mv
        u8g2.setFont(u8g2_font_siji_t_6x10);
        if(battLevel >= 55)
        {
            u8g2.drawGlyph(115, 6, 0xe086);
        } else if(battLevel < 55 && battLevel >= 25)
        {
            u8g2.drawGlyph(115, 6, 0xe085);
        } else if(battLevel < 25)
        {
            u8g2.drawGlyph(115, 6, 0xe084);
        }
        u8g2.setFont(u8g2_font_micro_mr);

        u8g2.drawLine(0, 6, 128, 6);

        // Draw our display buffer
        int16_t size = displayBuffer.size();
        for (int y = 0; y < size; y++) 
        {
            u8g2.drawStr(0, 13 + (y*6), displayBuffer[y].c_str());
        }
        u8g2.sendBuffer();
    }
}

/**
 * @brief Sends text to display
 * We want to refresh everytime info is added
 * to the display so we have the most up to date
 * info.
 * 
 * @param s String to send.
 * 
 */
void sendToDisplay(std::string s)
{
    if(!s.empty())
    {
        // Get display buffer size
        int16_t size = displayBuffer.size();

        // MAX 9 lines Y
        if(size < 9)
        {
            // MAX 32 characters X
            if(s.length() <= 32)
            {
                // Push the newest data to the back of array
                displayBuffer.push_back(s);
            } else {
                // If text too long, resize then push to back
                s.resize(30);
                displayBuffer.push_back(s + "..");
            }
        } else {
            // Erase oldest data and push newest to the back of array
            displayBuffer.erase(displayBuffer.begin());
            // MAX 32 characters X
            if(s.length() <= 32)
            {
                // Push the newest data to the back of array
                displayBuffer.push_back(s);
            } else {
                // If text too long, resize then push to back
                s.resize(30);
                displayBuffer.push_back(s + "..");
            }
        }
        refreshDisplay();
    }
}

/**
 * @brief RX Counter
 * Max 999 because of screen limits
 * TODO: Track how many resets (1000 beacons sent)
 * 
 */
void rxCounter()
{
    if(rxCount < 999)
    {
        rxCount++;
        refreshDisplay();
    } else {
        rxCount = 0;
        txCount = 0;
        sendToDisplay("Resetting RX/TX Beacon Count!");
    }
}

/**
 * @brief TX Counter
 * Max 999 because of screen limits
 * 
 */
void txCounter()
{
    if(txCount < 999)
    {
        txCount++;
        refreshDisplay();
    } else {
        rxCount = 0;
        txCount = 0;
        sendToDisplay("Resetting RX/TX Beacon Count!");
    }
}

void parseJSON(std::string input)
{
    // DEBUG
    if(input.empty())
    {
        input = "{\"name\":\"glamorous-hazel-mule (+1)\",\"rssi\":-93,\"snr\":12.2,\"lat\":47.5179,\"long\":-52.8124}";
    }

    // Total (minimum) 152
    // Total (recommended) 192
    // Using 256 to leave room as 'input' is not static
    DynamicJsonDocument jsonObj(256);
    DeserializationError error = deserializeJson(jsonObj, input);

    if(!error)
    {
        const char* hsName = jsonObj["name"];
        int16_t hsRssi = jsonObj["rssi"].as<int16_t>();
        float hsSnr =  jsonObj["snr"].as<float>();
        double hsLat = jsonObj["lat"].as<double>();
        double hsLong = jsonObj["long"].as<double>();
        if(hsName != nullptr)
        {
            rxCounter();
            
            std::ostringstream namess;
            namess << hsName;
            std::string hsNameS = namess.str();

            std::ostringstream snrss;
            snrss << std::fixed << std::setprecision(1) << hsSnr;

            std::string rxrssi = std::to_string(g_last_rssi);
            std::string rxsnr = std::to_string(g_last_snr);

            double distM = 0.0;
            double distKM = 0.0;
            std::string distS = "";
            // THIS CHECK IS FOR DEBUGGING ONLY
            if (my_rak1910_gnss.location.isValid())
		    {
                distM = my_rak1910_gnss.distanceBetween(ftester_lat, ftester_long, hsLat, hsLong);
                distKM = distM / 1000.0;
                if(distKM <= 0.1)
                {
                    distS = "<0.1";
                } else {
                    std::ostringstream dss;
                    dss << std::fixed << std::setprecision(1) << distKM;
                    distS = dss.str();
                }
            } else {
                // DEBUG
                ftester_lat = 47.51563224351281;
                ftester_long = -52.81891231773771;
                distM = my_rak1910_gnss.distanceBetween(ftester_lat, ftester_long, hsLat, hsLong);
                distKM = distM / 1000.0;
                if(distKM <= 0.1)
                {
                    distS = "<0.1";
                } else {
                    std::ostringstream dssd;
                    dssd << std::fixed << std::setprecision(1) << distKM;
                    distS = dssd.str();
                }
            } 

            // Distance and HS count is critical info, so we deal with it differently
            // This needs to mimic displayName
            std::string lenCheck = std::to_string(rxCount) + "." + hsNameS + " " + distS + "km";            
            int16_t nameLen = lenCheck.length();

            // Nibble away at hot spot name to save
            // important info (i.e. HS count/distance)
            // Most dynamic way I can think to do this
            if(nameLen > 32)
            {
                std::string delimiter = "-";
                // Find first '-'
                int16_t pos = hsNameS.find(delimiter);
                // Chunk size to fit on screen
                int16_t chunk = nameLen - 32;
                // Erase chunk, but keep first '-'
                hsNameS.erase(pos+1, chunk);
            }

            std::string displayName = std::to_string(rxCount) + "." + hsNameS + " " + distS + "km";
            std::string signalInfo = "RSSI:" + rxrssi + "/" + std::to_string(hsRssi) + " SNR:" + rxsnr + "/" + snrss.str();

            sendToDisplay(displayName);
            sendToDisplay(signalInfo);
        }
    }
}

/**
 * @brief Put display into Power Saver mode
 * Mostly to save burn in. leave screen on if
 * data is being processed
 * 
 * @param unused 
 * 
 */
void ftester_display_sleep(TimerHandle_t unused)
{
    if(!ftester_busy)
    {
        displayOn = false;
        u8g2.setPowerSave(true);
    }
}

/**
 * @brief Get battery level from API
 * 
 * @return float 
 */
float ftester_getBattLevel()
{
    return mv_to_percent(read_batt());
}

/**
 * @brief Used to update battery level in a timer
 * 
 * @param unused 
 */
void ftester_updateBattLevel(TimerHandle_t unused)
{
    if(!ftester_busy)
    {
        battLevel = ftester_getBattLevel();
    }
}

/**
 * @brief Field Testers event handler
 * 
 */
void ftester_event_handler(void)
{
    if((g_task_event_type & LORA_JOIN_FIN) == LORA_JOIN_FIN)
    {
        g_task_event_type &= N_LORA_JOIN_FIN;
        if(g_join_result)
        {
            sendToDisplay("Joined Helium Network!");
            // Simulating RX
            // for(int x=0; x < 10; x++)
            // {
            //     parseJSON("");
            // }
            // Don't turn off screen until joined Helium
            // Bad for screen but usable for now
            displayTimeoutTimer.begin(305137, ftester_display_sleep);
            displayTimeoutTimer.start();
            battTimer.begin(65317, ftester_updateBattLevel, NULL, true);
            battTimer.start();
        }
    }
}

/**
 * @brief Field Testers LoRa Data handler
 * Parse incoming(RX) JSON data from LLIS
 * Convert from HEX to string
 * Build JSON obj
 * Iterate through JSON obj
 * 
 */
void ftester_lora_data_handler(void)
{
    ftester_busy = true;
    // Convert RX data buffer to char
    char log_buff[g_rx_data_len * 3] = {0};
    uint8_t log_idx = 0;
    for (int idx = 0; idx < g_rx_data_len; idx++)
    {
        sprintf(&log_buff[log_idx], "%02X ", g_rx_lora_data[idx]);
        log_idx += 3;
    }

    std::string jsonString;
    std::string hexString = std::string(log_buff);
    // Remove spaces
    std::string::iterator end_pos = std::remove(hexString.begin(), hexString.end(), ' ');
    hexString.erase(end_pos, hexString.end());

    // Convert from HEX to "text" to build JSON string
    int16_t len = hexString.length();
    for(int i = 0; i < len; i+=2)
    {
        std::string byte = hexString.substr(i,2);
        char chr = (char) (int)strtol(byte.c_str(), nullptr, 16);
        jsonString.push_back(chr);
    }
    parseJSON(jsonString);
    ftester_busy = false;
}

/**
 * @brief Field Tester accelerometer event
 * Turn off power saver mode and start timeout again
 * 
 */
void ftester_acc_event(void)
{
    if(displayOn)
    {
        // Keep display on
        displayTimeoutTimer.reset();
    } else {
        // Screen is off, wake up
        u8g2.setPowerSave(false);
        displayTimeoutTimer.reset();
        displayOn = true;
    }
}

/**
 * @brief Mapper is sending beacon(packet)
 * Keep count of how many beacons sent
 * Max of 999 because of screen limitations
 * 
 */
void ftester_tx_beacon(void)
{
    txCounter();
}

/**
 * @brief Set GPS data for device
 * 
 * @param lat 
 * @param lon 
 */
void ftester_setGPSData(int64_t lat, int64_t lon)
{
    ftester_lat = lat / 100000.0;
    ftester_long = lon / 100000.0;
}

/**
 * @brief Set the GPS fix status of the tester
 * 
 * @param fix mapper passed info
 */
void ftester_gps_fix(bool fix)
{
    ftester_satCount = my_rak1910_gnss.satellites.value();

    if(fix)
    {
        if(!ftester_gpsLock)
        {
            sendToDisplay("GPS satellites found!");
            ftester_gpsLock = true;
        }
    } else {
        if(ftester_gpsLock)
        {
            sendToDisplay("Lost GPS fix.");
        } else {
            sendToDisplay("Searching for GPS satellites.");
        }
        ftester_gpsLock = false;
    }
}

/**
 * @brief Initialize Display here
 * 
 */
void ftester_init(void)
{
    u8g2.begin();
    u8g2.setFont(u8g2_font_micro_mr);
    u8g2.drawXBM(0, 0, rak_width, rak_height, rak_bits);
    u8g2.drawStr(74, 10, "R4K v0.3a");
    u8g2.drawStr(74, 16, "Field Tester");
    u8g2.drawStr(74, 28, "Alpha Build");
    u8g2.drawStr(38, 64, "Joining Helium Network!");
    u8g2.sendBuffer();
}