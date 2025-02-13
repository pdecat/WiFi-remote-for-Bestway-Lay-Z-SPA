#include "BWC54149E_8266.h"

CIO *pointerToClass;
//BWC *pointerToBWC;

//set flag instead of saving. This may avoid crashes. Some functions appears to crash when called from a timer.
// static void tick(void){
	// pointerToBWC->saveSettingsFlag();
// }

static void IRAM_ATTR LEDdatapin(void) {
  pointerToClass->LEDdataHandler();
}
static void IRAM_ATTR clockpin(void) {
  pointerToClass->clkHandler();
}

void CIO::begin(int cio_td_pin, int cio_clk_pin, int cio_ld_pin) {
  pointerToClass = this;
  _CIO_TD_PIN = cio_td_pin;
  _CIO_CLK_PIN = cio_clk_pin;
  _CIO_LD_PIN = cio_ld_pin;
  pinMode(_CIO_LD_PIN, INPUT);
  pinMode(_CIO_TD_PIN, OUTPUT);
  pinMode(_CIO_CLK_PIN, INPUT);
  digitalWrite(_CIO_TD_PIN, 1); //idle high
  attachInterrupt(digitalPinToInterrupt(_CIO_LD_PIN), LEDdatapin, CHANGE);
  attachInterrupt(digitalPinToInterrupt(_CIO_CLK_PIN), clockpin, CHANGE); //Write on falling edge and read on rising edge
}

void CIO::stop(){
  detachInterrupt(digitalPinToInterrupt(_CIO_LD_PIN));
  detachInterrupt(digitalPinToInterrupt(_CIO_CLK_PIN));
}


//match 7 segment pattern to a real digit
char CIO::_getChar(uint8_t value) {
  for (unsigned int index = 0; index < sizeof(CHARCODES); index++) {
    if (value == CHARCODES[index]) {
      return CHARS[index];
    }
  }
  return '*';
}

void CIO::loop(void) {
	//newdata is true when a data packet has arrived from cio
	if(newData) {
		newData = false;
		static int capturePhase = 0;
		static uint32_t buttonReleaseTime;
		static uint16_t prevButton = ButtonCodes[NOBTN];
    /*
     * This model is only sending messages when something updated
     * so this section is not useful
     */
    /*
    //require two consecutive messages to be equal before registering
		static uint8_t prev_checksum = 0;
    uint8_t checksum = 0;
    for(unsigned int i = 0; i < sizeof(payload); i++){
			checksum += _payload[i];
		}
    if(checksum != prev_checksum) {
      prev_checksum = checksum;
      return;
    }
    prev_checksum = checksum;
     */

		//copy private array to public array
		for(unsigned int i = 0; i < sizeof(payload); i++){
			payload[i] = _payload[i];
		}

	  //determine if anything changed, so we can update webclients
		for(unsigned int i = 0; i < sizeof(payload); i++){
			if (payload[i] != _prevPayload[i]) dataAvailable = true;
			_prevPayload[i] = payload[i];
		}

		//brightness = _brightness & 7; //extract only the brightness bits (0-7)
		//extract information from payload to a better format
		states[LOCKEDSTATE] = (payload[LCK_IDX] & (1 << LCK_BIT)) > 0;
		states[POWERSTATE] = 1;  //(payload[PWR_IDX] & (1 << PWR_BIT)) > 0;
		states[UNITSTATE] = (payload[C_IDX] & (1 << C_BIT)) > 0;
		states[BUBBLESSTATE] = (payload[AIR_IDX] & (1 << AIR_BIT)) > 0;
		states[HEATGRNSTATE] = (payload[GRNHTR_IDX] & (1 << GRNHTR_BIT)) > 0;
		states[HEATREDSTATE] = (payload[REDHTR_IDX] & (1 << REDHTR_BIT)) > 0;
		states[HEATSTATE] = states[HEATGRNSTATE] || states[HEATREDSTATE];
		states[PUMPSTATE] = (payload[FLT_IDX] & (1 << FLT_BIT)) > 0;
		states[CHAR1] = (uint8_t)_getChar(payload[DGT1_IDX]);
		states[CHAR2] = (uint8_t)_getChar(payload[DGT2_IDX]);
		states[CHAR3] = (uint8_t)_getChar(payload[DGT3_IDX]);
    if(HASJETS) states[JETSSTATE] = (payload[HJT_IDX] & (1 << HJT_BIT)) > 0;
    else states[JETSSTATE] = 0;
		//Determine if display is showing target temp or actual temp or anything else.
		//capture TARGET after UP/DOWN has been pressed...
		if( ((button == ButtonCodes[UP]) || (button == ButtonCodes[DOWN])) && (prevButton != ButtonCodes[UP]) && (prevButton != ButtonCodes[DOWN]) ) capturePhase = 1;
		//...until 2 seconds after UP/DOWN released
		if( (button == ButtonCodes[UP]) || (button == ButtonCodes[DOWN]) ) buttonReleaseTime = millis();
		if(millis()-buttonReleaseTime > 2000) capturePhase = 0;
		//convert text on display to a value if the chars are recognized
    if(states[CHAR1] == '*' || states[CHAR2] == '*' || states[CHAR3] == '*') return;
		String tempstring = String((char)states[CHAR1])+String((char)states[CHAR2])+String((char)states[CHAR3]);
		uint8_t tmpTemp = tempstring.toInt();
		//capture only if showing plausible values (not blank screen while blinking)
		if( (capturePhase == 1) && (tmpTemp > 19) ) {
			states[TARGET] = tmpTemp;
		}
		//wait 4 seconds after UP/DOWN is released to be sure that actual temp is shown
		if( (capturePhase == 0) && (millis()-buttonReleaseTime > 10000) && payload[DGT3_IDX]!=0xED && payload[DGT3_IDX]!=0) states[TEMPERATURE] = tmpTemp;		
		prevButton = button;

    if(states[UNITSTATE] != _prevUNT || states[HEATSTATE] != _prevHTR || states[PUMPSTATE] != _prevFLT) {
      stateChanged = true;
      _prevUNT = states[UNITSTATE];
      _prevHTR = states[HEATSTATE];
      _prevFLT = states[PUMPSTATE];
    }
	}
}

//CIO comm
//packet start/stop
void IRAM_ATTR CIO::LEDdataHandler(void) {
  //Check START/END condition: _LD_PIN change when _CLK_PIN is high.
  if (READ_PERI_REG(PIN_IN) & (1 << _CIO_CLK_PIN)) {
    _byteCount = 0;
    _bitCount = 0;
    _received_cmd = 0;
    newData = READ_PERI_REG(PIN_IN) & (1 << _CIO_LD_PIN);
  }
}

void IRAM_ATTR CIO::clkHandler(void) {
  //read data on _cio_ld_pin and write to _cio_td_pin (LSBF)

  uint16_t td_bitnumber = _bitCount % 10;
  uint16_t ld_bitnumber = _bitCount % 8;
  uint16_t buttonwrapper = (B11111110 << 8) | (button<<1); //startbit @ bit0, stopbit @ bit9

  //rising or falling edge?
  bool risingedge = READ_PERI_REG(PIN_IN) & (1 << _CIO_CLK_PIN);
  if(risingedge){
    //clk rising edge
    _byteCount = _bitCount / 8;
    if(_byteCount == 0){
      _received_cmd |= ((READ_PERI_REG(PIN_IN) & (1 << _CIO_LD_PIN))>0) << ld_bitnumber;
    }
    else if( (_byteCount<6) && (_received_cmd == CMD2) ){ //only write to payload after CMD2. Also protect from buffer overflow
      //overwrite the old payload bit with new bit
      _payload[_byteCount-1] = (_payload[_byteCount-1] & ~(1 << ld_bitnumber)) | ((READ_PERI_REG(PIN_IN) & (1 << _CIO_LD_PIN))>0) << ld_bitnumber;
    }
    //store brightness in _cio local variable. It is not used, but put here in case we want to obey the pump.
    if(_bitCount == 7 && (_received_cmd & B11000000) == B10000000) _brightness = _received_cmd;
    _bitCount++;
  } else {
    //clk falling edge
    //first and last bit is a dummy start/stop bit (0/1), then 8 data bits in btwn
    if (buttonwrapper & (1 << td_bitnumber)) {
      WRITE_PERI_REG( PIN_OUT_SET, 1 << _CIO_TD_PIN);
    } else {
      WRITE_PERI_REG( PIN_OUT_CLEAR, 1 << _CIO_TD_PIN);
    }
  }
}

uint16_t DSP::getButton(void) {
  if(millis() - _dspLastGetButton > 20){
	  uint16_t newButton = 0;
	  _dspLastGetButton = millis();
    //startbit
    digitalWrite(_DSP_CLK_PIN, LOW);
    delayMicroseconds(CLKPW);
    digitalWrite(_DSP_CLK_PIN, HIGH);
    delayMicroseconds(CLKPW);
    //clock in 8 data bits
    for(int i = 0; i < 8; i++){
      digitalWrite(_DSP_CLK_PIN, LOW);
      delayMicroseconds(CLKPW);
      digitalWrite(_DSP_CLK_PIN, HIGH);
      newButton |= digitalRead(_DSP_TD_PIN)<<i;
      delayMicroseconds(CLKPW);
    }
    //stop bit
    digitalWrite(_DSP_CLK_PIN, LOW);
    delayMicroseconds(CLKPW);
    digitalWrite(_DSP_CLK_PIN, HIGH);
    //work around for glitches. Only register change after two consecutive and equal values
    if(newButton == _prevButton){
      _oldButton = newButton;
    } else {
      _prevButton = newButton;
    }
  }
  return (_oldButton);
}


//bitsToSend can only be 8 with this solution of LSB first
void DSP::_sendBitsToDSP(uint32_t outBits, int bitsToSend) {
  for (int i = 0; i < bitsToSend; i++) {
    digitalWrite(_DSP_CLK_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(_DSP_LD_PIN, outBits & (1 << i));
    delayMicroseconds(CLKPW-5);
    digitalWrite(_DSP_CLK_PIN, HIGH);
    delayMicroseconds(CLKPW);
  }
}

uint16_t DSP::_receiveBitsFromDSP() {
  return 0;
}

char DSP::_getCode(char value) {
  for (unsigned int index = 0; index < sizeof(CHARS); index++) {
    if (value == CHARS[index]) {
      return CHARCODES[index];
    }
  }
  return 0x00;  //no match, return 'space'
}

void DSP::updateDSP(uint8_t brightness) {
	 //refresh display with ~10Hz
	if(millis() -_dspLastRefreshTime > 99){
		_dspLastRefreshTime = millis();
		  digitalWrite(_DSP_LD_PIN, LOW); //start of packet
      delayMicroseconds(CLKPW);
		  _sendBitsToDSP(CMD1, 8);
      //end of packet: clock low, make sure LD is low before rising clock then LD
      digitalWrite(_DSP_CLK_PIN, LOW);
		  digitalWrite(_DSP_LD_PIN, LOW);
      delayMicroseconds(CLKPW);
      digitalWrite(_DSP_CLK_PIN, HIGH);
      delayMicroseconds(CLKPW);
		  digitalWrite(_DSP_LD_PIN, HIGH);
      delayMicroseconds(CLKPW);

		  digitalWrite(_DSP_LD_PIN, LOW); //start of packet
      delayMicroseconds(CLKPW);
		  _sendBitsToDSP(CMD2, 8);
      for(unsigned int i=0; i<sizeof(payload); i++){
        _sendBitsToDSP(payload[i], 8);
      }
      //end of packet: clock low, make sure LD is low before rising clock then LD
      digitalWrite(_DSP_CLK_PIN, LOW);
		  digitalWrite(_DSP_LD_PIN, LOW);
      delayMicroseconds(CLKPW);
      digitalWrite(_DSP_CLK_PIN, HIGH);
      delayMicroseconds(CLKPW);
		  digitalWrite(_DSP_LD_PIN, HIGH);
      delayMicroseconds(CLKPW);
      
		  digitalWrite(_DSP_LD_PIN, LOW); //start of packet
      delayMicroseconds(CLKPW);
		  _sendBitsToDSP((CMD3 & 0xF8) | brightness, 8);
      //end of packet: clock low, make sure LD is low before rising clock then LD
      digitalWrite(_DSP_CLK_PIN, LOW);
		  digitalWrite(_DSP_LD_PIN, LOW);
      delayMicroseconds(CLKPW);
      digitalWrite(_DSP_CLK_PIN, HIGH);
      delayMicroseconds(CLKPW);
		  digitalWrite(_DSP_LD_PIN, HIGH);
      delayMicroseconds(CLKPW);
	}
}

void DSP::textOut(String txt) {
  int len = txt.length();
  if (len >= 3) {
    for (int i = 0; i < len - 2; i++) {
      payload[DGT1_IDX] = _getCode(txt.charAt(i));
      payload[DGT2_IDX] = _getCode(txt.charAt(i + 1));
      payload[DGT3_IDX] = _getCode(txt.charAt(i + 2));
      updateDSP(7);
      delay(230);
    }
  }
  else if (len == 2) {
    payload[DGT1_IDX] = _getCode(' ');
    payload[DGT2_IDX] = _getCode(txt.charAt(0));
    payload[DGT3_IDX] = _getCode(txt.charAt(1));
    updateDSP(7);
  }
  else if (len == 1) {
    payload[DGT1_IDX] = _getCode(' ');
    payload[DGT2_IDX] = _getCode(' ');
    payload[DGT3_IDX] = _getCode(txt.charAt(0));
    updateDSP(7);
  }
}

void DSP::LEDshow() {
  //todo: clear payload first...
  for(unsigned int y = 0; y < sizeof(payload); y++){
    for(int x = 0; x < 9; x++){
      payload[y] = (1 << x);
      updateDSP(7);
      delay(200);
    }
  }
}

void DSP::begin(int dsp_td_pin, int dsp_clk_pin, int dsp_ld_pin, int dsp_audio_pin) {
	_DSP_TD_PIN = dsp_td_pin;
	_DSP_CLK_PIN = dsp_clk_pin;
	_DSP_LD_PIN = dsp_ld_pin;
	_DSP_AUDIO_PIN = dsp_audio_pin;

	pinMode(_DSP_LD_PIN, OUTPUT);
	pinMode(_DSP_TD_PIN, INPUT);
	pinMode(_DSP_CLK_PIN, OUTPUT);
	pinMode(_DSP_AUDIO_PIN, OUTPUT);
	digitalWrite(_DSP_LD_PIN, HIGH); 	//idle high
	digitalWrite(_DSP_CLK_PIN, HIGH); //shift on falling, latch on rising
	digitalWrite(_DSP_AUDIO_PIN, LOW);
}

void DSP::playIntro() {
  int longnote = 125;
  int shortnote = 63;

  tone(_DSP_AUDIO_PIN, NOTE_C7, longnote);
  delay(2 * longnote);
  tone(_DSP_AUDIO_PIN, NOTE_G6, shortnote);
  delay(2 * shortnote);
  tone(_DSP_AUDIO_PIN, NOTE_G6, shortnote);
  delay(2 * shortnote);
  tone(_DSP_AUDIO_PIN, NOTE_A6, longnote);
  delay(2 * longnote);
  tone(_DSP_AUDIO_PIN, NOTE_G6, longnote);
  delay(2 * longnote);
  //paus
  delay(2 * longnote);
  tone(_DSP_AUDIO_PIN, NOTE_B6, longnote);
  delay(2 * longnote);
  tone(_DSP_AUDIO_PIN, NOTE_C7, longnote);
  delay(2 * longnote);
  noTone(_DSP_AUDIO_PIN);
}

//silent beep instead of annoying beeps every time something changes
void DSP::beep() {
  //int longnote = 125;
  // int shortnote = 63;
  // tone(_AUDIO_PIN, NOTE_C6, shortnote);
  // delay(shortnote);
  // tone(_AUDIO_PIN, NOTE_C7, shortnote);
  // delay(shortnote);
  // noTone(_AUDIO_PIN);
}

//new beep for button presses only
void DSP::beep2() {
  //int longnote = 125;
  int shortnote = 40;
  tone(_DSP_AUDIO_PIN, NOTE_D6, shortnote);
  delay(shortnote);
  tone(_DSP_AUDIO_PIN, NOTE_D7, shortnote);
  delay(shortnote);
  tone(_DSP_AUDIO_PIN, NOTE_D8, shortnote);
  delay(shortnote);
  noTone(_DSP_AUDIO_PIN);
}

BWC::BWC(){}

void BWC::begin(void){
	_cio.begin(D7, D2, D1);
	_dsp.begin(D5, D4, D3, D6);
	begin2();
}

void BWC::begin(
			int cio_td_pin,
			int cio_clk_pin,
			int cio_ld_pin,
			int dsp_td_pin,
			int dsp_clk_pin,
			int dsp_ld_pin,
			int dsp_audio_pin
			)
			{
	//start CIO and DSP modules
	_cio.begin(cio_td_pin, cio_clk_pin, cio_ld_pin);
	_dsp.begin(dsp_td_pin, dsp_clk_pin, dsp_ld_pin, dsp_audio_pin);
	begin2();
}

void BWC::begin2(){
  //Initialize variables
  _dspBrightness = 7; //default = max brightness
  _cltime = 0;
  _ftime = 0;
  _uptime = 0;
  _pumptime = 0;
  _heatingtime = 0;
  _airtime = 0;
  _jettime = 0;
  _timezone = 0;
  _price = 1;
  _finterval = 30;
  _clinterval = 14;
  _audio = true;
  _restoreStatesOnStart = false;
	_dsp.textOut(F("   hello   "));
	//_startNTP();
	LittleFS.begin();
	_loadSettings();
	_loadCommandQueue();
	_saveRebootInfo();
  _restoreStates();
	if(_audio) _dsp.playIntro();
  _dsp.LEDshow();
	saveSettingsTimer.attach(3600.0, std::bind(&BWC::saveSettingsFlag, this));
  _tttt = 0;
  _tttt_calculated = 0;
  _tttt_time0 = DateTime.now()-3600;
  _tttt_time1 = DateTime.now();
  _tttt_temp0 = 20;
  _tttt_temp1 = 20;
}

void BWC::stop(){
  _cio.stop();
}

void BWC::loop(){
  //feed the dog
  ESP.wdtFeed();
  ESP.wdtDisable();

	if (!DateTime.isTimeValid()) {
      //Serial.println("Failed to get time from server, retry.");
      DateTime.begin();
    }
	_timestamp = DateTime.now();

  //update DSP payload (memcpy(dest*, source*, len))
  //memcpy(&_dsp.payload[0], &_cio.payload[0], 11);
  for(unsigned int i = 0; i < sizeof(_dsp.payload); i++){
	  _dsp.payload[i] = _cio.payload[i];
  }
  _dsp.updateDSP(_dspBrightness);
  _updateTimes();
 //update cio public payload
  _cio.loop();
  //manage command queue
  _handleCommandQ();
  _handleButtonQ();//queue overrides real buttons
  if(_saveEventlogNeeded) saveEventlog();
  if(_saveCmdqNeeded) _saveCommandQueue();
  if(_saveSettingsNeeded) saveSettings();
  if(_cio.stateChanged) {
    _saveStates();
    _cio.stateChanged = false;
  }
  if(_saveStatesNeeded) _saveStates();
  //if set target command overshot we need to correct that
  if( (_cio.states[TARGET] != _latestTarget) && (_qButtonLen == 0) && (_latestTarget != 0) && (_sliderPrio) ) qCommand(SETTARGET, _latestTarget, 0, 0);
  //if target temp is unknown, find out.
  if( (_cio.states[TARGET] == 0) && (_qButtonLen == 0) ) qCommand(GETTARGET, (uint32_t)' ', 0, 0);

  //calculate time (in seconds) to target temperature
  //these variables can change anytime in the interrupts so copy them first
  uint8_t temperature = _cio.states[TEMPERATURE];
  uint8_t target = _cio.states[TARGET];
  //uint8_t unit = _cio.states[UNITSTATE];
  if(temperature != _tttt_temp1){
    _tttt_temp0 = _tttt_temp1;
    _tttt_temp1 = temperature;
    _tttt_time0 = _tttt_time1;
    _tttt_time1 = _timestamp;
  }
  int dtemp = _tttt_temp1 - _tttt_temp0;  //usually 1 or -1
  int dtime = _tttt_time1 - _tttt_time0;
  if(dtemp != 0 && abs(dtemp)<2) {        //if dtemp is larger we probably have a bad reading
    _tttt_calculated = (target-_tttt_temp1) * dtime/dtemp;
  } 
  _tttt = _tttt_calculated - _timestamp + _tttt_time1;
  ESP.wdtEnable(0);
}

//save out debug text to file "debug.txt" on littleFS
void BWC::saveDebugInfo(String s){
  File file = LittleFS.open("debug.txt", "a");
  if (!file) {
    Serial.println(F("Failed to save debug.txt"));
    return;
  }

  DynamicJsonDocument doc(1024);

  // Set the values in the document
  doc["timestamp"] = DateTime.format(DateFormatter::SIMPLE);
  doc["message"] = s;
  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write debug.txt"));
  }
  file.close();  
}


int BWC::_CodeToButton(uint16_t val){
	for(unsigned int i = 0; i < sizeof(ButtonCodes)/sizeof(uint16_t); i++){
		if(val == ButtonCodes[i]) return i;
	}
	return 0;
}

void BWC::_qButton(uint32_t btn, uint32_t state, uint32_t value, uint32_t maxduration) {
	if(_qButtonLen == MAXBUTTONS) return;	//maybe textout an error message if queue is full?
	_buttonQ[_qButtonLen][0] = btn;
	_buttonQ[_qButtonLen][1] = state;
	_buttonQ[_qButtonLen][2] = value;
	_buttonQ[_qButtonLen][3] = maxduration + millis();
	_qButtonLen++;
}

void BWC::_handleButtonQ(void) {
	if(_qButtonLen > 0){
		//check if state is as desired, or duration is up. If so - remove row. Else set BTNCODE
		if( (_cio.states[_buttonQ[0][1]] == _buttonQ[0][2]) || (millis() > _buttonQ[0][3]) ){
			if(_buttonQ[0][0] == UP || _buttonQ[0][0] == DOWN) maxeffort = false;
			//remove row
			for(int i = 0; i < _qButtonLen-1; i++){
				_buttonQ[i][0] = _buttonQ[i+1][0];
				_buttonQ[i][1] = _buttonQ[i+1][1];
				_buttonQ[i][2] = _buttonQ[i+1][2];
				_buttonQ[i][3] = _buttonQ[i+1][3];
			}
			_qButtonLen--;
			_cio.button = ButtonCodes[NOBTN];
		} else {
			if(_buttonQ[0][0] == UP || _buttonQ[0][0] == DOWN) maxeffort = true;
			//set buttoncode
			_cio.button = ButtonCodes[_buttonQ[0][0]];
		}
	} else {
    static uint16_t prevbtn = ButtonCodes[NOBTN];
		//no queue so let dsp value through
		uint16_t pressedButton = _dsp.getButton();
    int index = _CodeToButton(pressedButton);
    //if button is not enabled, NOBTN will result (buttoncodes[0])
		_cio.button = ButtonCodes[index*EnabledButtons[index]];
		//prioritize manual temp setting by not competing with the set target command
		if (pressedButton == ButtonCodes[UP] || pressedButton == ButtonCodes[DOWN]) _sliderPrio = false;
    //make noise
    if(_audio)
    {
      if((index*EnabledButtons[index]) & (prevbtn == ButtonCodes[NOBTN]))
      {
        _dsp.beep2();
      } 
    }
    prevbtn = pressedButton;
	}

}

bool BWC::qCommand(uint32_t cmd, uint32_t val, uint32_t xtime, uint32_t interval) {
	//handle special commands
	if(cmd == RESETQ){
		_qButtonLen = 0;
		_qCommandLen = 0;
		_saveCommandQueue();
		return true;
	}

	//add parameters to _commandQ[rows][parameter columns] and sort the array on xtime.
	int row = _qCommandLen;
	if (_qCommandLen == MAXCOMMANDS) return false;
	//sort array on xtime
	for (int i = 0; i < _qCommandLen; i++) {
		if (xtime < _commandQ[i][2]){
			//insert row at [i]
			row = i;
			break;
		}
	}
	//make room for new row
	for (int i = _qCommandLen; i > (row); i--){
		_commandQ[i][0] = _commandQ[i-1][0];
		_commandQ[i][1] = _commandQ[i-1][1];
		_commandQ[i][2] = _commandQ[i-1][2];
		_commandQ[i][3] = _commandQ[i-1][3];
	}
	//add new command
	_commandQ[row][0] = cmd;
	_commandQ[row][1] = val;
	_commandQ[row][2] = xtime;
	_commandQ[row][3] = interval;
	_qCommandLen++;
	delay(0);
	_saveCommandQueue();
	return true;
}

void BWC::_handleCommandQ(void) {
	bool restartESP = false;
	if(_qCommandLen > 0) {
	//cmp time with xtime. If more, then execute (adding buttons to buttonQ).

		if (_timestamp >= _commandQ[0][2]){
			_qButton(POWER, POWERSTATE, 1, 5000); //press POWER button until states[POWERSTATE] is 1, max 5000 ms
			_qButton(LOCK, LOCKEDSTATE, 0, 5000); //press LOCK button until states[LOCKEDSTATE] is 0
			switch (_commandQ[0][0]) {
				case SETTARGET:
					_latestTarget = _commandQ[0][1];
					//Fiddling with the hardware buttons is ignored while this command executes.
					_sliderPrio = true;
					//choose which direction to go (up or down)
					if(_cio.states[TARGET] == 0 )
					{
						_qButton(UP, TARGET, _commandQ[0][1], 10000);
						_qButton(DOWN, TARGET, _commandQ[0][1], 10000);
					}
					if(_cio.states[TARGET] > _commandQ[0][1]) _qButton(DOWN, TARGET, _commandQ[0][1], 10000);
					if(_cio.states[TARGET] < _commandQ[0][1]) _qButton(UP, TARGET, _commandQ[0][1], 10000);
					break;
				case SETUNIT:
					_qButton(UNIT, UNITSTATE, _commandQ[0][1], 5000);
					break;
				case SETBUBBLES:
					_qButton(BUBBLES, BUBBLESSTATE, _commandQ[0][1], 5000);
					break;
				case SETHEATER:
					_qButton(HEAT, HEATSTATE, _commandQ[0][1], 5000);
					break;
				case SETPUMP:
					_qButton(PUMP, PUMPSTATE, _commandQ[0][1], 5000);
					break;
				case REBOOTESP:
					restartESP = true;
					break;
				case GETTARGET:
					_qButton(UP, CHAR3, _commandQ[0][1], 700);
					break;
				case RESETTIMES:
					_uptime = 0;
					_pumptime = 0;
					_heatingtime = 0;
					_airtime = 0;
					_uptime_ms = 0;
					_pumptime_ms = 0;
					_heatingtime_ms = 0;
					_airtime_ms = 0;
					_cost = 0;
					_saveSettingsNeeded = true;
					break;
				case RESETCLTIMER:
					_cltime = _timestamp;
					_saveSettingsNeeded = true;
					break;
				case RESETFTIMER:
					_ftime = _timestamp;
					_saveSettingsNeeded = true;
					break;
				case SETJETS:
					_qButton(HYDROJETS, JETSSTATE, _commandQ[0][1], 5000);
					break;
        case SETBRIGHTNESS:
          _dspBrightness = _commandQ[0][1] & 7;
          break;
			}
			//If interval > 0 then append to commandQ with updated xtime.
			if(_commandQ[0][3] > 0) qCommand(_commandQ[0][0],_commandQ[0][1],_commandQ[0][2]+_commandQ[0][3],_commandQ[0][3]);
			//remove from commandQ and decrease qCommandLen
			for(int i = 0; i < _qCommandLen-1; i++){
			_commandQ[i][0] = _commandQ[i+1][0];
			_commandQ[i][1] = _commandQ[i+1][1];
			_commandQ[i][2] = _commandQ[i+1][2];
			_commandQ[i][3] = _commandQ[i+1][3];
			}
			_qCommandLen--;
			_saveCommandQueue();
			if(restartESP) {
				saveSettings();
				ESP.restart();
			}
		}
	}
}


String BWC::getJSONStates() {
    // Allocate a temporary JsonDocument
    // Don't forget to change the capacity to match your requirements.
    // Use arduinojson.org/assistant to compute the capacity.
  //feed the dog
  ESP.wdtFeed();
    DynamicJsonDocument doc(1024);

    // Set the values in the document
    doc["CONTENT"] = "STATES";
    doc["LCK"] = _cio.states[LOCKEDSTATE];
    doc["PWR"] = _cio.states[POWERSTATE];
    doc["UNT"] = _cio.states[UNITSTATE];
    doc["AIR"] = _cio.states[BUBBLESSTATE];
    doc["GRN"] = _cio.states[HEATGRNSTATE];
    doc["RED"] = _cio.states[HEATREDSTATE];
    doc["FLT"] = _cio.states[PUMPSTATE];
    doc["TGT"] = _cio.states[TARGET];
    doc["TMP"] = _cio.states[TEMPERATURE];
    doc["CH1"] = _cio.states[CHAR1];
    doc["CH2"] = _cio.states[CHAR2];
    doc["CH3"] = _cio.states[CHAR3];
    doc["HJT"] = _cio.states[JETSSTATE];
    doc["BRT"] = _dspBrightness;

    // Serialize JSON to string
    String jsonmsg;
    if (serializeJson(doc, jsonmsg) == 0) {
      jsonmsg = "{\"error\": \"Failed to serialize message\"}";
	}
	return jsonmsg;
}

String BWC::getJSONTimes() {
    // Allocate a temporary JsonDocument
    // Don't forget to change the capacity to match your requirements.
    // Use arduinojson.org/assistant to compute the capacity.
  //feed the dog
  ESP.wdtFeed();
    DynamicJsonDocument doc(1024);

    // Set the values in the document
    doc["CONTENT"] = "TIMES";
    doc["TIME"] = _timestamp;
    doc["CLTIME"] = _cltime;
    doc["FTIME"] = _ftime;
    doc["UPTIME"] = _uptime + _uptime_ms/1000;
    doc["PUMPTIME"] = _pumptime + _pumptime_ms/1000;    
    doc["HEATINGTIME"] = _heatingtime + _heatingtime_ms/1000;
    doc["AIRTIME"] = _airtime + _airtime_ms/1000;
    doc["JETTIME"] = _jettime + _jettime_ms/1000;
    doc["COST"] = _cost;
    doc["FINT"] = _finterval;
    doc["CLINT"] = _clinterval;
    doc["KWH"] = _cost/_price;
    doc["TTTT"] = _tttt;

    // Serialize JSON to string
    String jsonmsg;
    if (serializeJson(doc, jsonmsg) == 0) {
      jsonmsg = "{\"error\": \"Failed to serialize message\"}";
	}
	return jsonmsg;
}

String BWC::getJSONSettings(){
    // Allocate a temporary JsonDocument
    // Don't forget to change the capacity to match your requirements.
    // Use arduinojson.org/assistant to compute the capacity.
  //feed the dog
  ESP.wdtFeed();
    DynamicJsonDocument doc(1024);

    // Set the values in the document
    doc["CONTENT"] = "SETTINGS";
    doc["TIMEZONE"] = _timezone;
    doc["PRICE"] = _price;
    doc["FINT"] = _finterval;
    doc["CLINT"] = _clinterval;
    doc["AUDIO"] = _audio;
    doc["REBOOTINFO"] = ESP.getResetReason();
    doc["REBOOTTIME"] = DateTime.getBootTime();
    doc["RESTORE"] = _restoreStatesOnStart;

    // Serialize JSON to string
    String jsonmsg;
    if (serializeJson(doc, jsonmsg) == 0) {
      jsonmsg = "{\"error\": \"Failed to serialize message\"}";
	}
	return jsonmsg;
}

void BWC::setJSONSettings(String message){
  //feed the dog
  ESP.wdtFeed();
  DynamicJsonDocument doc(1024);

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.println(F("Failed to read config file"));
    return;
  }

  // Copy values from the JsonDocument to the variables
  _timezone = doc["TIMEZONE"];
  _price = doc["PRICE"];
  _finterval = doc["FINT"];
  _clinterval = doc["CLINT"];
  _audio = doc["AUDIO"];
  _restoreStatesOnStart = doc["RESTORE"];
  saveSettings();
}

String BWC::getJSONCommandQueue(){
  //feed the dog
  ESP.wdtFeed();
  DynamicJsonDocument doc(1024);
  // Set the values in the document
  doc["LEN"] = _qCommandLen;
  for(int i = 0; i < _qCommandLen; i++){
	  doc["CMD"][i] = _commandQ[i][0];
	  doc["VALUE"][i] = _commandQ[i][1];
	  doc["XTIME"][i] = _commandQ[i][2];
	  doc["INTERVAL"][i] = _commandQ[i][3];
  }

  // Serialize JSON to file
  String jsonmsg;
  if (serializeJson(doc, jsonmsg) == 0) {
    jsonmsg = "{\"error\": \"Failed to serialize message\"}";
  }
  return jsonmsg;
}

bool BWC::newData(){
	if(maxeffort) return false;	
  bool result = _cio.dataAvailable;
	_cio.dataAvailable = false;
	if (result && _audio) _dsp.beep();
  return result;
}

void BWC::_startNTP() {
  // setup this after wifi connected
  DateTime.setServer("pool.ntp.org");
  DateTime.begin();
  DateTime.begin();
  int c = 0;
  while (!DateTime.isTimeValid()) {
    Serial.println(F("Failed to get time from server. Trying again."));
    delay(1000);
    //DateTime.setServer("time.cloudflare.com");
    DateTime.begin();
    if (c++ > 5) break;
  }
  Serial.println(DateTime.format(DateFormatter::SIMPLE));
}

void BWC::_loadSettings(){
  File file = LittleFS.open("settings.txt", "r");
  if (!file) {
    Serial.println(F("Failed to load settings.txt"));
    return;
  }
  DynamicJsonDocument doc(1024);

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Failed to read settings.txt"));
    file.close();
    return;
  }

  // Copy values from the JsonDocument to the variables
  _cltime = doc["CLTIME"];
  _ftime = doc["FTIME"];
  _uptime = doc["UPTIME"];
  _pumptime = doc["PUMPTIME"];
  _heatingtime = doc["HEATINGTIME"];
  _airtime = doc["AIRTIME"];
  _jettime = doc["JETTIME"];
  _timezone = doc["TIMEZONE"];
  _price = doc["PRICE"];
  _finterval = doc["FINT"];
  _clinterval = doc["CLINT"];
  _audio = doc["AUDIO"];
  _restoreStatesOnStart = doc["RESTORE"];
  file.close();
}

void BWC::saveSettingsFlag(){
	//ticker fails if duration is more than 71 min. So we use a counter every 60 minutes
	if(++_tickerCount >= 3){
		_saveSettingsNeeded = true;
		_tickerCount = 0;
	}
}

void BWC::saveSettings(){
  if(maxeffort) {
	  _saveSettingsNeeded = true;
	  return;
  }
  //kill the dog
  ESP.wdtDisable();
  _saveSettingsNeeded = false;
  File file = LittleFS.open("settings.txt", "w");
  if (!file) {
    Serial.println(F("Failed to save settings.txt"));
    return;
  }

  DynamicJsonDocument doc(1024);
	_heatingtime += _heatingtime_ms/1000;
	_pumptime += _pumptime_ms/1000;
	_airtime += _airtime_ms/1000;
	_jettime += _jettime_ms/1000;
	_uptime += _uptime_ms/1000;
	_heatingtime_ms = 0;
	_pumptime_ms = 0;
	_airtime_ms = 0;
	_uptime_ms = 0;
  // Set the values in the document
  doc["CLTIME"] = _cltime;
  doc["FTIME"] = _ftime;
  doc["UPTIME"] = _uptime;
  doc["PUMPTIME"] = _pumptime;
  doc["HEATINGTIME"] = _heatingtime;
  doc["AIRTIME"] = _airtime;
  doc["JETTIME"] = _jettime;
  doc["TIMEZONE"] = _timezone;
  doc["PRICE"] = _price;
  doc["FINT"] = _finterval;
  doc["CLINT"] = _clinterval;
  doc["AUDIO"] = _audio;
  doc["SAVETIME"] = DateTime.format(DateFormatter::SIMPLE);
  doc["RESTORE"] = _restoreStatesOnStart;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write json to settings.txt"));
  }
  file.close();
  DateTime.begin();
  //revive the dog
  ESP.wdtEnable(0);

}

void BWC::_loadCommandQueue(){
  File file = LittleFS.open("cmdq.txt", "r");
  if (!file) {
    Serial.println(F("Failed to read cmdq.txt"));
    return;
  }

  DynamicJsonDocument doc(1024);
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Failed to deserialize cmdq.txt"));
    file.close();
    return;
  }

  // Set the values in the variables
  _qCommandLen = doc["LEN"];
  for(int i = 0; i < _qCommandLen; i++){
	  _commandQ[i][0] = doc["CMD"][i];
	  _commandQ[i][1] = doc["VALUE"][i];
	  _commandQ[i][2] = doc["XTIME"][i];
	  _commandQ[i][3] = doc["INTERVAL"][i];
  }

  file.close();
}

void BWC::_saveCommandQueue(){
  if(maxeffort) {
	  _saveCmdqNeeded = true;
	  return;
  }
  //kill the dog
  ESP.wdtDisable();

  _saveCmdqNeeded = false;
  File file = LittleFS.open("cmdq.txt", "w");
  if (!file) {
    Serial.println(F("Failed to save cmdq.txt"));
    return;
  }

  DynamicJsonDocument doc(1024);

  // Set the values in the document
  doc["LEN"] = _qCommandLen;
  for(int i = 0; i < _qCommandLen; i++){
	  doc["CMD"][i] = _commandQ[i][0];
	  doc["VALUE"][i] = _commandQ[i][1];
	  doc["XTIME"][i] = _commandQ[i][2];
	  doc["INTERVAL"][i] = _commandQ[i][3];
  }

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write cmdq.txt"));
  }
  file.close();
  //revive the dog
  ESP.wdtEnable(0);

}

void BWC::reloadCommandQueue(){
	  _loadCommandQueue();
	  return;
}

void BWC::reloadSettings(){
	  _loadSettings();
	  return;
}

void BWC::_saveStates() {
  if(maxeffort) {
	  _saveStatesNeeded = true;
	  return;
  }
  //kill the dog
  ESP.wdtDisable();

  _saveStatesNeeded = false;
  File file = LittleFS.open("states.txt", "w");
  if (!file) {
    Serial.println(F("Failed to save states.txt"));
    return;
  }

  DynamicJsonDocument doc(1024);

  // Set the values in the document
  doc["UNT"] = _cio.states[UNITSTATE];
  doc["HTR"] = _cio.states[HEATSTATE];
  doc["FLT"] = _cio.states[PUMPSTATE];

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write states.txt"));
  }
  file.close();
  //revive the dog
  ESP.wdtEnable(0);
}

void BWC::_restoreStates() {
  if(!_restoreStatesOnStart) return;
  File file = LittleFS.open("states.txt", "r");
  if (!file) {
    Serial.println(F("Failed to read states.txt"));
    return;
  }
  DynamicJsonDocument doc(1024);
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Failed to deserialize states.txt"));
    file.close();
    return;
  }

  uint8_t unt = doc["UNT"];
  uint8_t flt = doc["FLT"];
  uint8_t htr = doc["HTR"];
  qCommand(SETUNIT, unt, DateTime.now()+10, 0);
  qCommand(SETPUMP, flt, DateTime.now()+12, 0);
  qCommand(SETHEATER, htr, DateTime.now()+14, 0);

  file.close();
}

void BWC::saveEventlog(){
  if(maxeffort) {
	  _saveEventlogNeeded = true;
	  return;
  }
  _saveEventlogNeeded = false;
  //kill the dog
  ESP.wdtDisable();
  File file = LittleFS.open("eventlog.txt", "a");
  if (!file) {
    Serial.println(F("Failed to save eventlog.txt"));
    return;
  }

  DynamicJsonDocument doc(1024);

  // Set the values in the document
  for(unsigned int i = 0; i < sizeof(_cio.states); i++){
	doc[i] = _cio.states[i];
  }
  doc["timestamp"] = DateTime.format(DateFormatter::SIMPLE);

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write eventlog.txt"));
  }
  file.close();
  //revive the dog
  ESP.wdtEnable(0);

}

void BWC::_saveRebootInfo(){
  File file = LittleFS.open("bootlog.txt", "a");
  if (!file) {
    Serial.println(F("Failed to save bootlog.txt"));
    return;
  }

  DynamicJsonDocument doc(1024);

  // Set the values in the document
  doc["BOOTINFO"] = ESP.getResetReason() + " " + DateTime.format(DateFormatter::SIMPLE);

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write bootlog.txt"));
  }
  file.println();
  file.close();
}

void BWC::_updateTimes(){
	uint32_t now = millis();
	static uint32_t prevtime = now;
	int elapsedtime = now-prevtime;

	prevtime = now;
	if (elapsedtime < 0) return; //millis() rollover every 49 days
	if(_cio.states[HEATREDSTATE]){
		_heatingtime_ms += elapsedtime;
	}
	if(_cio.states[PUMPSTATE]){
		_pumptime_ms += elapsedtime;
	}
	if(_cio.states[BUBBLESSTATE]){
		_airtime_ms += elapsedtime;
	}
	if(_cio.states[JETSSTATE]){
		_jettime_ms += elapsedtime;
	}
	_uptime_ms += elapsedtime;

	if(_uptime_ms > 1000000000){
		_heatingtime += _heatingtime_ms/1000;
		_pumptime += _pumptime_ms/1000;
		_airtime += _airtime_ms/1000;
		_jettime += _jettime_ms/1000;
		_uptime += _uptime_ms/1000;
		_heatingtime_ms = 0;
		_pumptime_ms = 0;
		_airtime_ms = 0;
		_jettime_ms = 0;
		_uptime_ms = 0;
	}

	_cost = _price*(
                  (_heatingtime+_heatingtime_ms/1000)/3600.0 * 1900 + //s -> h ->Wh
                  (_pumptime+_pumptime_ms/1000)/3600.0 * 40 +
                  (_airtime+_airtime_ms/1000)/3600.0 * 800 +
                  (_uptime+_uptime_ms/1000)/3600.0 * 2 +
                  (_jettime+_jettime_ms/1000)/3600.0 * 400
                  )/1000.0; //Wh -> kWh
}

void BWC::print(String txt){
	_dsp.textOut(txt);
}

uint8_t BWC::getState(int state){
	return _cio.states[state];
}

String BWC::getPressedButton(){
	uint16_t btn = _dsp.getButton();
	uint8_t hib, lob;
	String s;
	hib = (uint8_t)(btn>>8);
	lob = (uint8_t)(btn & 0xFF);
	s = hib < 16 ? "0" + String(hib, HEX) : String(hib, HEX);
	s += lob < 16 ? "0" + String(lob, HEX) : String(lob, HEX);
	return  s;
}

String BWC::getButtonName() {
  return ButtonNames[_CodeToButton(_dsp.getButton() )];
}