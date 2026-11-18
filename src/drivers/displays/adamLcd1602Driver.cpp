// Adam: driver for a 16x2 character LCD (HD44780 + PCF8574 I2C backpack).
// Pages rotate every 5 s: hashrate/best diff, shares/blocks/temp, IP/RSSI, uptime/time.

#include "displayDriver.h"

#ifdef ADAM_LCD1602_DISPLAY

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "monitor.h"
#include <WiFi.h>

#ifndef ADAM_LCD_PAGE_MS
#define ADAM_LCD_PAGE_MS 5000
#endif

extern uint64_t upTime;

static LiquidCrystal_I2C lcd(LCD1602_ADDR, 16, 2);
static bool lcdBacklight = true;

// Print exactly 16 chars on a row (pads/truncates, no leftover characters)
static void lcdRow(uint8_t row, const char *text)
{
    char buf[17];
    snprintf(buf, sizeof(buf), "%-16.16s", text);
    lcd.setCursor(0, row);
    lcd.print(buf);
}

static void lcd1602_Init(void)
{
    Serial.println("LCD1602 (PCF8574) display driver initialized");
    Wire.begin(SDA_PIN, SCL_PIN);
    lcd.init();
    lcd.backlight();
    lcd.clear();
}

static void lcd1602_AlternateScreenState(void)
{
    lcdBacklight = !lcdBacklight;
    if (lcdBacklight) lcd.backlight();
    else lcd.noBacklight();
}

static void lcd1602_AlternateRotation(void) {}

static void lcd1602_LoadingScreen(void)
{
    Serial.println("Loading...");
    lcd.clear();
#ifdef ADAM_NO_BRAND
    lcdRow(0, "Becoming a");
    lcdRow(1, "millionaire...");
#else
    lcdRow(0, "NerdMiner v2");
    lcdRow(1, "Loading...");
#endif
}

static void lcd1602_SetupScreen(void)
{
    Serial.println("Setup mode...");
    lcd.clear();
    lcdRow(0, "WiFi:NerdMinerAP");
    lcdRow(1, "PW:MineYourCoins");
}

static void lcd1602_MainScreen(unsigned long mElapsed)
{
    mining_data data = getMiningData(mElapsed);
    Serial.printf(">>> Hashrate: %s KH/s | Shares: %s | Best diff: %s\n",
                  data.currentHashRate.c_str(), data.completedShares.c_str(), data.bestDiff.c_str());

    char l0[24], l1[24];
    uint8_t page = (millis() / ADAM_LCD_PAGE_MS) % 4;
    switch (page)
    {
    case 0:
        snprintf(l0, sizeof(l0), "Hash:%skH/s", data.currentHashRate.c_str());
        snprintf(l1, sizeof(l1), "Best:%s", data.bestDiff.c_str());
        break;
    case 1:
        snprintf(l0, sizeof(l0), "Shares:%s Blk:%s", data.completedShares.c_str(), data.valids.c_str());
        snprintf(l1, sizeof(l1), "Tmpl:%s T:%sC", data.templates.c_str(), data.temp.c_str());
        break;
    case 2:
        snprintf(l0, sizeof(l0), "IP  RSSI:%ddBm", (int)WiFi.RSSI());
        snprintf(l1, sizeof(l1), "%s", WiFi.localIP().toString().c_str());
        break;
    default:
    {
        uint64_t t = upTime;
        unsigned secs = t % 60; t /= 60;
        unsigned mins = t % 60; t /= 60;
        unsigned hours = t % 24;
        unsigned days = t / 24;
        snprintf(l0, sizeof(l0), "Up:%ud %02u:%02u:%02u", days, hours, mins, secs);
        snprintf(l1, sizeof(l1), "Time:%s", data.currentTime.c_str());
        break;
    }
    }
    lcdRow(0, l0);
    lcdRow(1, l1);
}

static void lcd1602_DoLedStuff(unsigned long frame) {}
static void lcd1602_AnimateCurrentScreen(unsigned long frame) {}

CyclicScreenFunction adamLcd1602CyclicScreens[] = {
    lcd1602_MainScreen,
};

DisplayDriver adamLcd1602DisplayDriver = {
    lcd1602_Init,
    lcd1602_AlternateScreenState,
    lcd1602_AlternateRotation,
    lcd1602_LoadingScreen,
    lcd1602_SetupScreen,
    adamLcd1602CyclicScreens,
    lcd1602_AnimateCurrentScreen,
    lcd1602_DoLedStuff,
    SCREENS_ARRAY_SIZE(adamLcd1602CyclicScreens),
    0,
    16,
    2,
};

#endif // ADAM_LCD1602_DISPLAY
