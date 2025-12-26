# Smart-Bell-Sys
I designed and built a fully automated school bell system using an ESP32 microcontroller to replace traditional manual bell switches. The system allows precise, scheduled bell ringing based on real-time clock data and can be controlled wirelessly through a web-based interface.

The setup uses an ESP32 (Wi-Fi enabled), DS3231 RTC for accurate timekeeping, OLED display for live status and time, and a high-current relay module to safely switch the school’s main electric bell. The relay electrically isolates the low-voltage control circuit from the high-voltage bell line, making the system safe and reliable for real-world deployment.

Key features include:

Automated bell ringing based on configurable period schedules

Web-based control panel accessible via Wi-Fi (no app installation required)

Multiple bell presets for different timetables (regular days, exams, events)

Real-time clock synchronization using hardware RTC

On-device OLED display showing current time and system status

Manual override and emergency control support

Designed to retain schedules even after power loss

This project solves a real operational problem in schools by reducing human dependency, eliminating timing errors, and enabling flexible timetable management. The system is modular, low-cost, and scalable, making it suitable for deployment across multiple classrooms or entire school campuses.
