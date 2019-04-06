/*
\file   main.c

\brief  Main source file.

(c) 2018 Microchip Technology Inc. and its subsidiaries.

Subject to your compliance with these terms, you may use Microchip software and any
derivatives exclusively with Microchip products. It is your responsibility to comply with third party
license terms applicable to your use of third party software (including open source software) that
may accompany Microchip software.

THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
FOR A PARTICULAR PURPOSE.

IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
SOFTWARE.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mcc_generated_files/application_manager.h"
#include "mcc_generated_files/led.h"
#include "mcc_generated_files/sensors_handling.h"
#include "mcc_generated_files/cloud/cloud_service.h"
#include "mcc_generated_files/debug_print.h"
#include "mcc_generated_files/mcc.h"

//This handles messages published from the MQTT server when subscribed
void receivedFromCloud(uint8_t *topic, uint8_t *payload)
{
    char *toggleToken = "\"toggle\":";
    char *subString;

    if ((subString = strstr((char*)payload, toggleToken)))
    {
        LED_holdYellowOn( subString[strlen(toggleToken)] == '1' );
    }


    debug_printer(SEVERITY_NONE, LEVEL_NORMAL, "topic: %s", topic);
    debug_printer(SEVERITY_NONE, LEVEL_NORMAL, "payload: %s", payload);
}

// This will get called every CFG_SEND_INTERVAL only while we have a valid Cloud connection
void sendToCloud(void)
{
   static char json[70];

   // This part runs every  seconds
   int rawTemperature = SENSORS_getTempValue();
   int light = SENSORS_getLightValue();
   int len = sprintf(json, "{\"Light\":%d,\"Temp\":\"%d.%02d\"}", light,rawTemperature/100,abs(rawTemperature)%100);

   if (len >0) {
      CLOUD_publishData((uint8_t*)json, len);
      LED_flashYellow();
   }
}

ticks YELLOW_task_manager(void *param)
{
    LED_YELLOW_set_level( !LED_YELLOW_get_level() );     // toggle the YELLOW LED
    return true;    // keep me running
}

ticks GREEN_task_manager(void *param)
{
    LED_GREEN_set_level( !LED_GREEN_get_level() );     // toggle the GREEN LED
    return true;    // keep me running
}

ticks RED_task_manager(void *param)
{
    LED_RED_set_level( false );     // turn on the RED LED
    scheduler_kill_all();          // kill all
    return true;    // keep me running
}

strTask_t YELLOW_task = {YELLOW_task_manager,"YEL"};
strTask_t GREEN_task =  {GREEN_task_manager, "GRE"};
strTask_t RED_task =    {RED_task_manager,   "RED"};

ticks BLUE_task_manager(void *param)
{
    LED_BLUE_set_level( false );     // turn on the BLUE LED
    if (LED_BLUE_get_level()){
        scheduler_kill_task(&GREEN_task);     // kill the GREEN task
        scheduler_create_task(&RED_task, 5000);  // replace it with RED task
    }
    return false;    // run once only
}

strTask_t BLUE_task =   {BLUE_task_manager,  "BLU"};

int main(void)
{
    // temporarily disable -iot and run a scheduler test
    //   application_init();
    SYSTEM_Initialize();
    LED_test();
    ENABLE_INTERRUPTS();
    puts("RTC Scheduler Test");
    // create tasks
    scheduler_create_task(&GREEN_task,   50); // 2Hz blink
    scheduler_create_task(&YELLOW_task, 1000); // 0.5 Hz blink
    scheduler_create_task(&BLUE_task,   5000); // after 5 seconds

    while (1) {
        runScheduler();
    }

    return 0;
}
