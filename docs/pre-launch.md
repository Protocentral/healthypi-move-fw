HealthyPi Move
=============
An Open Source wearable vital-signs monitor in a watch form factor. 

![HealthyPi Move](/docs/images/main-with-encl.jpg)

HealthyPi Move is not just another smartwatch with a heart rate monitor, it is a complete vital signs monitoring and recording device on your wrist that can measure Electrocardiogram(ECG), Photoplethysmogram(PPG), SpO2, Blood Pressure (finger-based), EDA/GSR, Heartrate Variability (HRV), Respiration Rate, and Body Temperature. It is designed to be a wearable, easy-to-use, all-in-one vital signs monitoring solution with high accuracy. Powerful onboard processing is enabled by the Nordic nRF5340 dual-core System-on-chip (SoC) with BLE support which allows for real-time signal processing and wireless BLE transmission of the data, while the onboard flash allows reliable long-term recording in standard data formats for research applications.

HealthyPi Move is for anyone who wants to monitor their vital signs in real-time, record their vital signs for research purposes, or integrate vital signs monitoring into their own projects. It is designed to be easy to use, with a simple and intuitive interface that allows you to view your vital signs in real-time, record your vital signs for later analysis, and export your data in standard formats for further analysis. It is also designed to be flexible, with an open-source design that allows you to customize the device to suit your needs.

# Features and Specifications

- Parameters Monitored
    - ECG
    - PPG (from finger contact)
    - SpO2 (from finger contact)
    - Blood Pressure (from finger contact)
    - EDA/GSR
    - HRV
    - Respiration Rate
    - Temperature
- Nordic nRF5340 dual-core microcontroller
    - Bluetooth Low Energy (BLE) 5.2
    - Arm Cortex-M33 application processor
    - Arm Cortex-M33 network processor
- 1.28" TFT display with capacitive touch
- LSM6DSO 6-DoF IMU
- MAX30001 ECG and bio-impedance front-end
- MAX32664 Sensor Hub with MAX30101 optical PPG sensor
- On-board Real-time Clock (RTC) with battery backup
- 8 - 128 MB NOR QSPI flash memory for data storage
- 3.7V 150mAh LiPo battery with USB charging
- Firmware based on Zephyr RTOS
- Companion mobile app for Android
- Dimensions: 43 mm diameter, 16 mm thickness

# Open source from the ground up

The design of the HealthyPi Move is open-source from the ground up. The hardware design files, firmware, and software are all open-source and available on GitHub. The design is based on the Nordic nRF5340 SoC, which is supported by the Zephyr RTOS. The hardware design file are in Autodesk Eagle format and the enclosure design files are in Fusion 360 format. The accompanying mobile app is also open-source and written in Flutter for cross-platform support. 

![HealthyPi Move Teartown](/docs/images/healthypi-move-torndown.jpg)

1. What are the three most important things a potential backer should know about this project?


* This is not a smartwatch, instead it is a vital-signs monitor on your wrist. HealthyPi Move offers a wide range of vital signs monitoring capabilities, including ECG, PPG, SpO2, blood pressure, EDA/GSR, HRV, respiration rate, and body temperature, all packed into a convenient watch form factor.
* High Accuracy Data: Designed for clinical-grade monitoring and data collection applications, HealthyPi Move ensures accurate and reliable measurements, empowering users to track their health with confidence.
* Open-Source Flexibility: With an open-source design, HealthyPi Move allows for customization and integration into various projects, providing flexibility and adaptability to meet diverse user needs.

2. Who are your users and what problems does this project solve for them?

The users of HealthyPi Move span a wide range of individuals, including:

* Researchers and Clinicians: Professionals in healthcare and biomedical research fields who require accurate and reliable data for clinical studies, patient monitoring, and research projects. For this class, it solves the problem of having high accuracy and reliability in vital signs monitoring, while also maintaining study protocols and data integrity (being more mobile, it encourages user compliance). Long-term data recording and export capabilities are also essential for research applications.
* Health-conscious Individuals: Those who are proactive about monitoring their health and fitness levels, seeking a convenient and comprehensive solution to track vital signs and overall well-being. Although this is not a medical device, it provides a reliable and accurate way to monitor vital signs and track health metrics.
* DIY Enthusiasts and Developers: Makers, developers, and hobbyists interested in integrating vital signs monitoring into their own projects, experimenting with wearable technology, and exploring innovative applications. This includes product developers who want to build custom solutions for specific use cases or industries. These users currently have very solutions (usually based on breakout boards) that are really field-ready and reliable, and HealthyPi Move aims to fill that gap.

3. Why did you undertake this project? Give us a few sentences about your backstory.

This project is the result of our years of delivering open-source health monitoring solutions, starting with HealthyPi 2,3,4 and culminating in HealthyPi 5. These projects have been instrumental in democratizing health monitoring and making it accessible to a wide range of users. The problem though has been that it is a handheld device and doesn't really lend itself to ambulatory monitoring applications. Given our experience in developing wearable devices (starting with the HeartyPatch, which was later spun-off into its own company), HealthyPi Move was a natural progression for us. We wanted to take the capabilities of HealthyPi 5 and make it more accessible and convenient for users, by integrating it into a wearable form factor that is also field-ready. We saw that the future was "wearable" and decided to take the plunge and develop HealthyPi Move. We believe that the combination of open-source design and high accuray sensors will break barriers and enable a wide range of applications and users to benefit from this technology. This is even more truer now, given the "ecosystem-centric" digital health devices that are currently in the market that are not only expensive but also lock users into proprietary ecosystems, blocking interoperability. 

4. Are you happy with your project's name, or do you want suggestions for a new name?

We are happy with the name "HealthyPi Move". It captures the essence of the project - a health monitoring device that is mobile and moves with you. It also builds on the brand recognition of our previous projects, HealthyPi 2,3,4,5.

