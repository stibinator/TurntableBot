EESchema Schematic File Version 4
EELAYER 26 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L Driver_Motor:Pololu_Breakout_DRV8825 A?
U 1 1 5B7CE758
P 3500 2600
F 0 "A?" H 3500 3378 50  0000 C CNN
F 1 "Pololu_Breakout_DRV8825" H 3500 3287 50  0000 C CNN
F 2 "Module:Pololu_Breakout-16_15.2x20.3mm" H 3700 1800 50  0001 L CNN
F 3 "https://www.pololu.com/product/2982" H 3600 2300 50  0001 C CNN
	1    3500 2600
	1    0    0    -1  
$EndComp
$Comp
L Transistor_FET:2N7000 Q?
U 1 1 5B7CEB3C
P 5000 2950
F 0 "Q?" H 5205 2996 50  0000 L CNN
F 1 "2N7000" H 5205 2905 50  0000 L CNN
F 2 "Package_TO_SOT_THT:TO-92_Inline" H 5200 2875 50  0001 L CIN
F 3 "https://www.fairchildsemi.com/datasheets/2N/2N7000.pdf" H 5000 2950 50  0001 L CNN
	1    5000 2950
	1    0    0    -1  
$EndComp
$Comp
L Transistor_FET:2N7000 Q?
U 1 1 5B7CEC9C
P 5000 2300
F 0 "Q?" H 5205 2346 50  0000 L CNN
F 1 "2N7000" H 5205 2255 50  0000 L CNN
F 2 "Package_TO_SOT_THT:TO-92_Inline" H 5200 2225 50  0001 L CIN
F 3 "https://www.fairchildsemi.com/datasheets/2N/2N7000.pdf" H 5000 2300 50  0001 L CNN
	1    5000 2300
	1    0    0    -1  
$EndComp
$Comp
L Connector:Conn_01x08_Female J?
U 1 1 5B7CED8F
P 1500 2450
F 0 "J?" H 1527 2426 50  0000 L CNN
F 1 "Conn_01x08_Female" H 1527 2335 50  0000 L CNN
F 2 "" H 1500 2450 50  0001 C CNN
F 3 "~" H 1500 2450 50  0001 C CNN
	1    1500 2450
	1    0    0    -1  
$EndComp
Wire Wire Line
	1500 2150 2300 2150
Wire Wire Line
	2300 2150 2300 2800
Wire Wire Line
	2300 2800 3100 2800
Wire Wire Line
	1500 2250 2400 2250
Wire Wire Line
	2400 2250 2400 2700
Wire Wire Line
	2400 2700 3100 2700
Wire Wire Line
	1500 2350 2150 2350
Wire Wire Line
	2150 2350 2150 1500
Wire Wire Line
	2150 1500 4800 1500
Wire Wire Line
	4800 2300 4800 1500
Wire Wire Line
	1500 2450 2050 2450
Wire Wire Line
	2050 2450 2050 1600
Wire Wire Line
	2050 1600 4700 1600
Wire Wire Line
	4700 1600 4700 2950
Wire Wire Line
	4700 2950 4800 2950
Wire Wire Line
	1500 2850 2550 2850
Wire Wire Line
	2550 2850 2550 2600
Wire Wire Line
	2550 2600 3100 2600
$EndSCHEMATC
