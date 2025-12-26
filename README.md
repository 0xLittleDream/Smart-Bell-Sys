## Features
- Automatic bell scheduling using RTC
- Web-based timetable editor (Wi-Fi AP mode)
- OLED live time & system status
- Relay-isolated high-voltage switching
- Works offline after setup
- Power-failure safe (RTC + EEPROM)

## Why this matters
Schools still rely on manual bell systems that are error-prone and waste staff time.
This system automates the entire process at very low cost.

## Hardware Used
- ESP32
- DS3231 RTC
- 0.96" OLED
- 5V Relay Module
- 5V USB Power Supply

## How it Works
1. ESP32 creates a Wi-Fi access point
2. Admin connects via phone
3. Bell schedules are configured
4. ESP32 stores data locally
5. RTC triggers relay at exact times

## Future Improvements
- Multi-day timetables
- Cloud sync
- Admin authentication
- Mobile app
