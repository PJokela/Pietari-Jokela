#include <stdio.h>
#include <stdlib.h>

 

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

 

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <driverlib/timer.h>
#include <ti/mw/display/Display.h>
#include <ti/mw/display/DisplayExt.h>
#include <ti/mw/grlib/grlib.h>
#include <ti/drivers/UART.h>
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/drivers/i2c/I2CCC26XX.h>


/* Board Header files */
#include "Board.h"


#include "wireless/comm_lib.h"
#include "sensors/opt3001.h"
#include "sensors/mpu9250.h"
#include "buzzer.h"


/* Fat Stacks Of Tasks */
#define STACKSIZE 2048
Char sensorTaskStack  [STACKSIZE];
Char displayTaskStack [STACKSIZE];
Char commTaskStack    [STACKSIZE];
Char speakerTaskStack [STACKSIZE];


/*
Pinnejä vaativien nappien/ledien/MPU/kaiutin käsittelijät
0:ylänappi, 1:alanappi ja 1 punainen ledi ja 0 vihreä
*/
static PIN_Handle button0Handle;
static PIN_State  button0State;


static PIN_Handle button1Handle;
static PIN_State  button1State;


static PIN_Handle led0Handle;
static PIN_State  led0State;


static PIN_Handle led1Handle;
static PIN_State  led1State;


static PIN_Handle buzzerHandle;
static PIN_State  buzzerState;


static PIN_Handle hMpuPin;
static PIN_State  MpuPinState;
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};


static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};


/*Painikkeiden, ledien ja kaiuttimen konfiguraatiot*/
PIN_Config button0Config[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};


PIN_Config button1Config[] = {
   Board_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};


PIN_Config led0Config[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};


PIN_Config led1Config[] = {
   Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};


PIN_Config buzzerConfig[] = {
   Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};


//OMAT FUNKTIOT
void liikkeentunnistus(float gx, float gy);
void liikeundo(void);


/* PÄÄTILAKONE */
enum tila { ALKU = 1, PELITILA, VOITTO, HAVIO};
enum tila tilakone = ALKU;


/* LIIKKEEN TILAKONE
Käytetään globaalina muuttujana, jonka avulla näyttö, liikkeenkäsittely ja viestin lähetys tekevät oikeita asioita.
PAIKKA_V,... ,PAIKKA_A ovat välimuuttujia, jotka estävät liikkeen menemistä suoraan esim. vasemmalta oikealle.
Eli PAIKKA_V jne hyödyntävät näytön päivitystahtia, eli liike ei voi mennä toiseen ennenkuin näyttö on ns "tyhjä".
*/
enum LIIKE {PAIKKA = 1 ,VASEN, OIKEA, YLOS, ALAS, PAIKKA_V, PAIKKA_O, PAIKKA_Y, PAIKKA_A};
enum LIIKE LIIKAHDUS = PAIKKA;


/*GLOBAALIT LIIKEMUUTTUJAT UNDOTA, LIIKELASKUA ja VALOISUUTTA VARTEN*/
char liikemuisti[64];
int liikemaara = 0;
int liikeindeksi = 0;
char liikkeet[16];
char valoisuus[20];
char tempsana[64];


/* Painikkeiden keskeytyksien käsittelijäfunktiot*/
// Ylempi nappi
void button0Fxn(PIN_Handle handle, PIN_Id pinId) {
    /* Pelitilaan siirtyminen painikkeesta*/
    if(tilakone == ALKU) {
       tilakone = PELITILA;
    }
    //Pelitilasta poistuminen samasta painikkeesta ja liiketilakoneen resetointi paikalleen.
    else if(tilakone == PELITILA) {
       tilakone = ALKU;
       LIIKAHDUS = PAIKKA;
    }
    PIN_setOutputValue(led1Handle, Board_LED1, !PIN_getOutputValue( Board_LED1 ) );
}

// Alempi nappi
void button1Fxn(PIN_Handle handle, PIN_Id pinId) {
    //Päätösnäytöstä meneminen takaisin main menuun.
    if (tilakone == PELITILA)   {
        liikeundo();
    }
    if((tilakone == VOITTO) || (tilakone == HAVIO))   {
        tilakone  = ALKU;
        sprintf(liikkeet,"\0");
        liikemaara = 0;
        liikeindeksi = 0;
        LIIKAHDUS = PAIKKA;
    }
    PIN_setOutputValue(led0Handle, Board_LED0, !PIN_getOutputValue( Board_LED0 ) );
}


/* Anturien taskifunktio*/
void sensorFxn(UArg arg0, UArg arg1) {
    float ax, ay, az, gx, gy, gz;
    double pressure, temperature;
    int laskuri = 0;

    I2C_Handle i2c; // INTERFACE FOR OTHER SENSORS
    I2C_Params i2cParams;
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;

    I2C_Handle i2cMPU; // INTERFACE FOR MPU9250 SENSOR
    I2C_Params i2cMPUParams;
    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;

    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
        System_abort("Error Initializing I2CMPU\n");
    }

    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);

    Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    System_printf("MPU9250: Setup and calibration...\n");
    System_flush();

    mpu9250_setup(&i2cMPU);
    
    System_printf("MPU9250: Setup and calibration OK\n");
    System_flush();

    I2C_close(i2cMPU);

    i2c = I2C_open(Board_I2C, &i2cParams);
    if (i2c == NULL) {
        System_abort("Error Initializing I2C\n");
    }
    
    opt3001_setup(&i2c);
    
    I2C_close(i2c);
    while (1) {
        if (tilakone == PELITILA) {
            i2c = I2C_open(Board_I2C, &i2cParams);
            if (i2c == NULL) {
                System_abort("Error Initializing I2C\n");
            }
            
            laskuri = laskuri + 1;
            if (laskuri >= 6)   {
                float luxarvo;
                luxarvo = opt3001_get_data(&i2c);
                sprintf(valoisuus,"Lux: %.2f",luxarvo);
                laskuri = 0;
            }
            I2C_close(i2c);
            
            i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
            if (i2cMPU == NULL) {
                System_abort("Error Initializing I2CMPU\n");
            }
            mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);
            I2C_close(i2cMPU);
            
            liikkeentunnistus(gx,gy);
        }
        //Tulostetaan arvot debug-ikkunaan
        //char liike[80];
        //sprintf(liike,"%f,%f,%f\n",gx,gy,gz);
        //System_printf(liike);
        System_flush();
        Task_sleep((1000000/6) / Clock_tickPeriod);
    }
     PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_OFF);
}


/*Näytön taskifunktio*/
void displayTaskFxn(UArg arg0, UArg arg1){
    Display_Handle displayHandle;
    Display_Params params;
    Display_Params_init(&params);
    params.lineClearMode = DISPLAY_CLEAR_BOTH;
    
    displayHandle = Display_open(Display_Type_LCD, &params);
    
    char payload[16];
    
    while(1) {
        if (displayHandle) {
            if(tilakone == ALKU) {
               Display_print0(displayHandle, 1, 7,       "Y   GAME");
               Display_print0(displayHandle, 4, 4,    "WELCOME"    );
               Display_print0(displayHandle, 5, 1, "V    TO      O");
               Display_print0(displayHandle, 6, 5,     "TRON!"     );
               Display_print0(displayHandle, 10,7,       "A"       );
            }
            if(tilakone == PELITILA) {
                Display_print0(displayHandle, 1, 7,       "Y    MENU");
                Display_print0(displayHandle, 2, 2,   "EIKUN PELIA!" );
                Display_print0(displayHandle, 4, 3,   "YOUR MOVE:" );
                Display_print0(displayHandle, 7, 3,   valoisuus );
                
                if ((LIIKAHDUS == PAIKKA_O) || (LIIKAHDUS == PAIKKA_V) || (LIIKAHDUS == PAIKKA_A) || (LIIKAHDUS == PAIKKA_Y)) {
                    Display_print0(displayHandle, 5, 2,  " "  );
                    LIIKAHDUS = PAIKKA;
                }
                if (LIIKAHDUS == VASEN) {
                    Display_print0(displayHandle, 5, 5,  "VASEN"  );
                    sprintf(payload,"event:LEFT");
                    liikemuisti[liikemaara] = 'v';
                    //liikemaara = strlen(liikemuisti+1); // UUSI
                    liikemaara = liikemaara + 1;
                    //liikeindeksi = liikeindeksi + 1;
                }
                if (LIIKAHDUS == OIKEA) {
                    Display_print0(displayHandle, 5, 5,  "OIKEA"  );
                    sprintf(payload,"event:RIGHT");
                    liikemuisti[liikemaara] = 'o';
                   // liikemaara = strlen(liikemuisti); // UUSI
                    liikemaara = liikemaara + 1;
                    //liikeindeksi = liikeindeksi + 1;
                }
                if (LIIKAHDUS == YLOS) {
                    Display_print0(displayHandle, 5, 5,  "YLOS"  );
                    sprintf(payload,"event:UP");
                    liikemuisti[liikemaara] = 'y';
                   // liikemaara = strlen(liikemuisti); // UUSI
                    liikemaara = liikemaara + 1;
                    //liikeindeksi = liikeindeksi + 1;
                }
                if (LIIKAHDUS == ALAS) {
                    Display_print0(displayHandle, 5, 5,  "ALAS"  );
                    sprintf(payload,"event:DOWN");
                    liikemuisti[liikemaara] = 'a';
                    // liikemaara = strlen(liikemuisti); // UUSI
                    liikemaara = liikemaara + 1;
                    //liikeindeksi = liikeindeksi + 1;
                }
                Display_print0(displayHandle, 6, 1, "V            O");
                Display_print0(displayHandle, 10, 7,      "A   UNDO");

                if((LIIKAHDUS == OIKEA) || (LIIKAHDUS == VASEN) || (LIIKAHDUS == ALAS) || (LIIKAHDUS == YLOS))  {
                    Send6LoWPAN(0x1234, payload, strlen(payload));
                    // System_printf(liikemuisti);
                    // System_printf("\n");
                    //System_printf(liikemaara); 
                    StartReceive6LoWPAN();
                }
            }
            if(tilakone == VOITTO) {
                int i;
                sprintf(liikkeet,"Liikkeita: %d",liikeindeksi);
                Display_print0(displayHandle, 1, 1,liikkeet);
                for (i = 2; i <= 10; i++) { //Jos alempi rivi oikeen i muutetaan 2
                Display_print0(displayHandle, i, 4,   "VOITTO!"      );
                }
            }
            if(tilakone == HAVIO) {
                int i;
                sprintf(liikkeet,"Liikkeita: %d",liikemaara);
                Display_print0(displayHandle, 1, 1,liikkeet);
                for (i = 2; i <= 10; i++) { //Jos alempi rivi oikeen i muutetaan 2
                Display_print0(displayHandle, i, 1,   "HAVISIT PELIN"      );
                }
            }
            Task_sleep((1000000/2) / Clock_tickPeriod);
            Display_clear(displayHandle);
        }
    }
}


/* Kommunikaation ja liikkeenkäsittelyn taskifunktio*/
void commFxn(UArg arg0, UArg arg1) {
    char payload[16]; // viestipuskuri
    uint16_t senderAddr;

    // Radio alustetaan vastaanottotilaan
    int32_t result = StartReceive6LoWPAN();
    if(result != true) {
        System_abort("Wireless receive start failed");
    }
    // Vastaanotetaan viestejä loopissa
    while (true) {
        if (GetRXFlag()) {
            // Tyhjennetään puskuri (ettei sinne jäänyt edellisen viestin jämiä)
            memset(payload,0,16);
            // Luetaan viesti puskuriin payload
            Receive6LoWPAN(&senderAddr, payload, 16);
            // Tulostetaan vastaanotettu viesti konsoli-ikkunaan
            System_printf(payload);
            // Verrataan vastaanotettuja viestejä voitto/häviöehtoihin ja asetetaan tilakone
            if(strcmp(payload , "144,WIN") == 0){
                tilakone = VOITTO;
                sprintf(liikemuisti,"\0");
            }
            if(strcmp(payload , "144,LOST GAME") == 0){
                tilakone = HAVIO;
                sprintf(liikemuisti,"\0");
            }
            System_flush();
        }
    }
}


/* Kaiuttimen taski*/
void speakerFxn(UArg arg0, UArg arg1)   {
    int menumelodia[60] =
    { b4f, b4f, a4f, a4f,
        f5, f5, e5f, b4f, b4f, a4f, a4f, e5f, e5f, c5s, c5, b4f,
        c5s, c5s, c5s, c5s,
        c5s, e5f, c5, b4f, a4f, a4f, a4f, e5f, c5s,
        b4f, b4f, a4f, a4f,
        f5, f5, e5f, b4f, b4f, a4f, a4f, a5f, c5, c5s, c5, b4f,
        c5s, c5s, c5s, c5s,
        c5s, e5f, c5, b4f, a4f, a4f, e5f, c5s
    };
    int menurytmi[60] =
    { 1, 1, 1, 1,
        3, 3, 6, 1, 1, 1, 1, 3, 3, 3, 1, 2,
        1, 1, 1, 1,
        3, 3, 3, 1, 2, 2, 2, 4, 8,
        1, 1, 1, 1,
        3, 3, 6, 1, 1, 1, 1, 3, 3, 3, 1, 2,
        1, 1, 1, 1,
        3, 3, 3, 1, 2, 2, 2, 4, 8, 4
    };
    int tmp;
    int im;
    while(1)    {
        if ((tilakone == VOITTO) || (tilakone == HAVIO))   {
            buzzerOpen(buzzerHandle);
            for(im = 0; im <= 60; im++)  {
                tmp = menumelodia[im];
                buzzerSetFrequency(tmp);
                Task_sleep((menurytmi[im]*(1000000/8)) / Clock_tickPeriod); 
                buzzerSetFrequency(4);
                Task_sleep(((1000000/20)) / Clock_tickPeriod);
            }
            buzzerClose();
        }
        im = 0;
        Task_sleep((1000000/1) / Clock_tickPeriod);
    }
}


//Liikkeentunnistuksen funktio
void liikkeentunnistus(float gx, float gy)  {
    //TRESHOLD gyroantureiden arvoille
    int th = 150;
    
    //gy:n ehtorakenne oikealle
    if(gy > th) {
        if(LIIKAHDUS == PAIKKA) {
            LIIKAHDUS = OIKEA;
            //System_printf("Oikea\n");
        }
        else if (LIIKAHDUS == VASEN) {
            LIIKAHDUS = PAIKKA_V;
            //System_printf("Vasemmalta paikka\n");
        }
    }
    //gy:n ehtorakenne vasemmalle
    else if(gy < -th)   {
        if(LIIKAHDUS == PAIKKA) {
            LIIKAHDUS = VASEN;
            //System_printf("Vasen\n");
        }
        else if (LIIKAHDUS == OIKEA) {
            LIIKAHDUS = PAIKKA_O;
            //System_printf("Oikealta paikka\n");
        }
    }
    //gy:n ehtorakenne ylos
    else if(gx > th)    {
        if(LIIKAHDUS == PAIKKA) {
            LIIKAHDUS = YLOS;
            //System_printf("ylos\n");
        }
        else if (LIIKAHDUS == ALAS) {
            LIIKAHDUS = PAIKKA_A;
            //System_printf("alhaalta paikalleen\n");
        }
    }
    //gy:n ehtorakenne alas
    else if(gx < -th){
        if(LIIKAHDUS == PAIKKA) {
            LIIKAHDUS = ALAS;
            //System_printf("alas\n");
        }
        else if (LIIKAHDUS == YLOS) {
            LIIKAHDUS = PAIKKA_Y;
            //System_printf("ylhaalta paikalleen\n");
        }
    }
}


void liikeundo(void)    {
    char payload[16];
    if (liikemaara > 0)  {
        if (liikemuisti[liikemaara-1] == 'o')   {
            sprintf(payload,"event:LEFT");
        }
        else if (liikemuisti[liikemaara-1] == 'v')   {
            sprintf(payload,"event:RIGHT");
        }
        else if (liikemuisti[liikemaara-1]  == 'a')   {
            sprintf(payload,"event:UP");
        }
        else if (liikemuisti[liikemaara-1] == 'y')  {
            sprintf(payload,"event:DOWN");
        }
    Send6LoWPAN(0x1234, payload, strlen(payload));
    liikemaara = liikemaara - 1;
//    liikeindeksi = liikeindeksi + 1;
//    strncpy(tempsana,liikemuisti,liikemaara-1);
//    sprintf(liikemuisti, "\0");
//    strcpy(liikemuisti,tempsana);
//    sprintf(tempsana, "\0");
    System_printf(payload);
    System_printf("\n");
    StartReceive6LoWPAN();    
    }
}


int main(void) {
// Antureiden taski käsittelijä
    Task_Handle sensorTask;
    Task_Params sensorTaskParams;

// Näytön taskin käsittelijä
    Task_Handle displayTask;
    Task_Params displayTaskParams;

// Comm taskin käsittelijä
    Task_Handle commTask;
    Task_Params commTaskParams;
    
// Speaker taskin käsittelijä
    Task_Handle speakerTask;
    Task_Params speakerTaskParams;

    Board_initGeneral();
    Board_initI2C();
    Init6LoWPAN();

    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
     if (hMpuPin == NULL) {
        System_abort("Pin open failed!");
    }


    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority = 2;
    sensorTask = Task_create(sensorFxn, &sensorTaskParams, NULL);
    if (sensorTask == NULL) {
        System_abort("sensorTask create failed!");
    }

    Task_Params_init(&displayTaskParams);
    displayTaskParams.stackSize = STACKSIZE;
    displayTaskParams.stack = &displayTaskStack;
    displayTaskParams.priority = 2;
    displayTask = Task_create(displayTaskFxn, &displayTaskParams, NULL);
    if (displayTask == NULL) {
        System_abort("displayTask create failed!");
    }

    Task_Params_init(&commTaskParams);
    commTaskParams.stackSize = STACKSIZE;
    commTaskParams.stack = &commTaskStack;
    commTaskParams.priority = 1;
    commTask = Task_create(commFxn, &commTaskParams, NULL);
    if (commTask == NULL) {
        System_abort("commTask create failed!");
    }
   
    Task_Params_init(&speakerTaskParams);
    speakerTaskParams.stackSize = STACKSIZE;
    speakerTaskParams.stack = &speakerTaskStack;
    speakerTaskParams.priority = 2;
    speakerTask = Task_create(speakerFxn, &speakerTaskParams, NULL);
    if (speakerTask == NULL) {
        System_abort("speakerTask create failed!");
    }

 
    // Painikkeiden ja ledien alustus
    button0Handle = PIN_open(&button0State, button0Config);
    if(!button0Handle) {
       System_abort("Error initializing button0 pins\n");
    }

    button1Handle = PIN_open(&button1State, button1Config);
    if(!button1Handle) {
       System_abort("Error initializing button1 pins\n");
    }

    led0Handle = PIN_open(&led0State, led0Config);
    if(!led0Handle) {
       System_abort("Error initializing LED0 pins\n");
    }

    led1Handle = PIN_open(&led1State, led1Config);
    if(!led1Handle) {
       System_abort("Error initializing LED1 pins\n");
    }
    
    buzzerHandle = PIN_open(&buzzerState, buzzerConfig);
    if(!buzzerHandle) {
       System_abort("Error initializing LED1 pins\n");
    }

    if (PIN_registerIntCb(button0Handle, &button0Fxn) != 0) {
       System_abort("Error registering button0 callback function");
    }
    if (PIN_registerIntCb(button1Handle, &button1Fxn) != 0) {
       System_abort("Error registering button1 callback function");
    }

    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}