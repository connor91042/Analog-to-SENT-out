
//Table of contents:
//    1.Intro
//    2.Initialization
//    3.Main loop
//    4.Tools


//  ___________________________________________________________________________________________________
//|                                                                                                                                                                                                 
//|                              INTRO   1.                                                                                     
//|________________________________________________________________________________________________   
    //this code implements an analog input to 0.5us tick time SENT potocol output on teensy 3.2 
    //it uses a timer module (FLEX timer module or FTM) on the teensy to implement
    //this module allows a stack of seperate timer values to be pushed to timer for dynamic updating 
    //this is how SENT message is implmented.
    
    //Basic flow of operation is as follows:
    //teensy reads analog input in main loop and stores in variable  
    //when the sent line on teensy is pulled down, an interrupt is triggered that sets a request flag
    //in main loop, the request is served 
    //the saved analog value is converted into sent nibble (including CRC) then converted to corresponding timer values
    //the timer's stack is then stuffed with the message.
    //the timer ouputs message and request flag is cleared.

    
    //for more information on Teensy 3.2's ARM micro go to   
    //https://www.pjrc.com/teensy/K20P64M72SF1RM.pdf 
    //CH36 for more info on FTM flex timer
 
 
//___________________________________________________________________________________________________
//|                                                                                                                                                                                                 
//|                                                                                                                     
//|                           Initialization 2.
//|
//|
//|________________________________________________________________________________________________   
    //have to set all timer registers, init variables, set pin directions etc
    //sent line is attached to digital input pin 2 and pin 22 of teensy
    //pin 2 senses when line is pulled low, pin 22 outputs SENT signal to line. 
    //analog input attached to A0

 int SENT_MESSAGE_ARRAY[7];     //array holding sent nibble package counts for timer setup
 int wait_time = 103;           //time between message request and message output 
 int SCALE = 9;                 //how many times faster timer is than SENT tick. (timer counts for every 1 sent tick)

 volatile bool  send_flag = 0;   //flag set for request 
 int num =0;                    //number to contain analog value. 
 int N1 = 0;
 int N2 = 0; 
 int N3 = 0;
 int CRC = 0;


 //function prototypes so fucntions can be used at end of file
  int bitscan(int input);
  void software_delay(int us);
  int calc_crc(int input);

  


void setup() {
  pinMode(2, INPUT_PULLDOWN);                                      //set up sent line to pin as a pulldown
  attachInterrupt(digitalPinToInterrupt(2), set_flag , FALLING);   //attach interrupt to pin and target function
  pinMode(A0,INPUT);                                                      //analog input pin setup


//This section contatins timer initialization for FTM0
  FTM0_SC &= ~0x18;                  //Disable Timer
  SIM_SCGC6|=0x03000000;             //enable FTM0 and FTM0 module clock
  FTM0_CONF=0xC0;                    //set up BDM in 11
  FTM0_SC |= 0x1;                    //divide 72mhz system clock by PS = 4. It will be 9 ticks on this clock per 1 2Mhz tick


  //Connect FTM0 timer to Pin
  PORTC_PCR1 = PORT_PCR_MUX(4)|0x20; // FTM0 CH0 - Pin 22 on teensy///MAKE SURE YOU COUNT RIGHT 
                                     // or A8
  //Setup Edge aligned PWM MODE
  FTM0_QDCTRL &= 0xFFFFFFFE; //QUADEN = 0
  FTM0_C0SC &= 0x1FFFFFFF;   //COMBINE = 0 //DECAPEN = 0
  FTM0_C0SC |= 0x20;         //MES0B = 1 
  FTM0_C0SC |= 0x04;         //ELS0A = 1 this sets it to be low then high
  
  //Init FTM0 timer values
  FTM0_CNTIN=0;               //Set CNTIN
  FTM0_C0V = 5*SCALE;         //Set CV 
  FTM0_MOD= 56*SCALE;         //Set MOD Each value is n-1

  //Init sotware loading scheme. 
  FTM0_SYNC |= 0x3;        //CNTMAX = 1  load at overflow up counter 
  FTM0_SYNCONF |= 0x80;    //SYNCMODE = 1 enhanced pwm synchronizaiton scheme
  FTM0_SYNCONF |= 0x200;   //SWWRBUF = 1 enables software timer load
  FTM0_SYNCONF &= ~0x100;  //SWRSTCNT = 0 software trigger initiates pwm syncronization 
  FTM0_SYNC |= 0x80;       //SWSYNC When this is high, it means there is a value ready to be loaded in the buffer. 
  FTM0_SYNC &= ~0x80;      //clear it for init
}

void set_flag(){
 send_flag = 1;
  }


//_________________________________________________________________________________________________
//|                                                                                                                                                                                                 
//|                                                                                                                     
//|                           MAIN loop 3.
//|
//|
//|________________________________________________________________________________________________   


void loop() {

    num = 4*analogRead(A0);  //read analog in (multiply by 4 to get 12-bit value)
    
    
    if(send_flag){         //if Requested
      send_sent_message(); //send message
      send_flag = 0;       //reset Request flag   
      } 
}





int start_time = 0;
int end_time = 0;
int nibble_index = 0;
int offset_ = 27;


void send_sent_message(void){
  //send sent_message fucntion 
  //this function sets up the timer to produce sent message
  //the data corresponds to analog input value
  
  nibble_index = 1;    //reset value
  FTM0_MODE |= 0x2;    //force init values
  FTM0_OUTINIT &= 0x1; //initilization value
  
  
  //SENT message creation ________________________________
  
  //get data for nibbles
  N1 = (num >> 8)& 0xF;  
  N2 = (num >> 4)& 0xF;
  N3 = num & 0xF;
  CRC = calc_crc(num);       //determine CRC

  SENT_MESSAGE_ARRAY[0] = 56*SCALE;           //SYNC
  SENT_MESSAGE_ARRAY[1] = 12*SCALE;           //SlowSerial
  SENT_MESSAGE_ARRAY[2] = (N1+12)*SCALE;      // N1 MSN
  SENT_MESSAGE_ARRAY[3] = (N2+12)*SCALE;      // N2
  SENT_MESSAGE_ARRAY[4] = (N3+12)*SCALE;      // N3 LSN
  SENT_MESSAGE_ARRAY[5] = (CRC+12)*SCALE;     // CRC
  SENT_MESSAGE_ARRAY[6] = 3000;               //long placeholder, adds some time to update between messages

  //______________________________________________________



  
  //pack timer stack ________________________
  
  FTM0_C0V = 5*SCALE + offset_; //Stilll a bug to figure out, there is a bit of jitter in sync puls. offset 
  FTM0_MOD= SENT_MESSAGE_ARRAY[0]+offset_;  // the first message
  
  
  software_delay(wait_time);               //make the pulse wait a bit
  FTM0_SC |= 0x8;                          //Enable Timer
  FTM0_SYNC &= ~0x80;                      //when timer is enabled the sync bit is high, it needs to be low.
 
  //Sent sent message
  while(nibble_index <=7){ 
    
    if(!bitRead(FTM0_SYNC,7)){                      //if buffer if ready to write to 
      FTM0_MOD=SENT_MESSAGE_ARRAY[nibble_index];    //set new mod value
      FTM0_C0V = 5*SCALE;
      FTM0_SYNC |= 0x80;                            //flip the sync flag
      nibble_index++;                               //index nibble
      }
  }
  
  FTM0_SC &= ~0x18; //Disable Timer
  //________________________________________
}






//_________________________________________________________________________________________________
//|                                                                                                                                                                                                 
//|                                                                                                                     
//|                           Tools 4.
//|
//|
//|________________________________________________________________________________________________ 
//this section contains all the functions used by the main functions. And anything else useful



int i = 0;
int polynomial = 0x1D; //polynomial for CRC 
int seed = 0x5;        //start value of CRC
int CRC_len = 4;       //number of bits of crc


int CRC_val = 0;

int calc_crc(int input){
  //this funtion takes in input values and returns the crc code
  //the crc parameters are set using variable. 

  //pad values
  input |= (seed<<12); //add crc seed to FRONTTTTT
  input = input << CRC_len; //pad input value
  
  
  //pad polynomial number
  int poly = polynomial;
  poly = poly << (bitscan(input) - bitscan(poly)); //pad the poly to line up with msb of input
  
  while(bitscan(input)>(CRC_len-1)){ //do CRC xor operations until all the zeros are gone.   Check wikipedia CRC for explanation :D
    //print_binary(input);
    //print_binary(poly);
    input = input ^ poly;//xor operation 
    poly = poly >> (bitscan(poly) - bitscan(input)); //shift poly to line up with the msb of new input
    
    }
  
    return input;
  
  }







void software_delay(int us){
  //this function is just a patch for now
  //it delays the software in micro seconds roughly the amount of us
  start_time = micros();
  end_time = start_time - 1000;
  while((end_time-start_time)<us){
    end_time = micros();
    }
  }


int bitscan(int input){
  //this funtion taks in the input 
  //and returns the index of the first bit that is set
  
  for(i=30; i>=0; i--){
    
    if(bitRead(input,i)){
      return i;
      }
    }
  return 0;
  }




//Test nibble
//  SENT_MESSAGE_ARRAY[0] = 56*SCALE; //SYNC
//  SENT_MESSAGE_ARRAY[1] = 12*SCALE; //SlowSerial
//  SENT_MESSAGE_ARRAY[2] = 20*SCALE; // N1 MSN
//  SENT_MESSAGE_ARRAY[3] = 20*SCALE; // N2
//  SENT_MESSAGE_ARRAY[4] = 24*SCALE; // N3 LSN
//  SENT_MESSAGE_ARRAY[5] = 20*SCALE; // CRC
//  SENT_MESSAGE_ARRAY[6] = 3000; //long placeholder
// 
