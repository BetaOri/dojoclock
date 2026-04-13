/* Count Up/Down Timer */
// Mar 5, 2026: Updated from original behavior with #1, #2, #3, #4 below. Janssen w/ CoPilot.

/*
  Helpful Corrections by:
	Github user Mannelito - Correction to ResumeTimer function
*/
/*
The MIT License (MIT)

Copyright (c) 2016 Andrew Mascolo Jr

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#ifndef CountUpDownTimer_h
#define CountUpDownTimer_h

#include<Arduino.h>
#define UP 1
#define DOWN 0

class CountUpDownTimer
{
	public:
    CountUpDownTimer(bool type, bool precision = HIGH) : _type(type), _precision(precision) { 
		SetStopTime((type? 0xFFFFFFFF : 0)); // 18h 12m 15s
		time = precision ? micros() : millis();
		//remainingSeconds = 0;
		SetTimer(0);
		reset = false, stop = true, paused = true;
		timeFlag = false;
		duration = precision ? 1000000 : 1000;
	}
	
	bool started = false;

	unsigned long _InternalClock() {
		return _precision ? micros() : millis();
	}
	
void Timer() { 
    timeFlag = false;
    if (!started) { // #1) Prevent timer from running before StartTimer() is called
        return; 
    }
    if (stop == true) { // Stop means fully frozen
        return; 
    }
    if (paused == true) {  // Paused means hold the current time reference
        time = _InternalClock(); 
        return; 
    }
    if ((_intTime = _InternalClock()) - time >= duration) { // Check if the duration interval has elapsed
        if (_type == UP) { // Update remaining time
            remainingSeconds++;
        } 
        else { // DOWN
            remainingSeconds--;
            if (remainingSeconds <= 0) { // #2) Auto-stop and clamp at zero
                remainingSeconds = 0;
                stop = true;
                timeFlag = true;   // fire the "tick" so TimeCheck() works
                time = _intTime;   // update reference
                return;            // exit cleanly
            }
        }
        timeFlag = true; // A normal tick occurred
        time = _intTime; // Update reference time

        if (_intTime < time) { // Handle micros() rollover
            time = 0;
        }
    }
}

//	void Timer() {
//		timeFlag = false;
//	  if (!started) { return; } // <-- Added Variable "Started" to stop timer() from advancing time to fix library.
//		if (stop == true) { return; }
//		if (paused == true) { time = _InternalClock(); return; }
//	      
//		if ((_intTime = _InternalClock() ) - time >= duration ) { // check the time difference and see if 1 second has elapsed
//			_type == UP? remainingSeconds++ : remainingSeconds--;
//			
//			timeFlag = true;
//
//			if ((_type == DOWN && remainingSeconds == 0) || TimeCheck()) // check to see if the clock is 0
//				stop = true; // If so, stop the timer
//			
//			time = _intTime;
//		  
//			if(_intTime < time) 
//				time = 0;  // check to see if micros() has rolled over, if not, then increment "time" by duration
//		}
//	}


void ResetTimer() {
    remainingSeconds     = 0;
    resetClockSeconds    = 0;
    started              = false;
    stop                 = true;
    paused               = true;
    time                 = _InternalClock();
    watch                = time;
    timeFlag             = false;
}


//	void ResetTimer() {
//		if(_type) 
//			remainingSeconds = 0;
//		else
//			SetTimer(resetClockSeconds);
//		
//		// stop = false; // #3
//		started = false;
//	}


void StartTimer() {
    watch   = _InternalClock();
    time    = watch;
    started = true;
    stop    = false;
    paused  = false;
    // Do NOT modify remainingSeconds or stop time here
}

//	void StartTimer() {
//		watch = _InternalClock();
//		started = true;
//		stop = false;
//		paused = false;
//		if(_type == UP) 
//			remainingSeconds = 0;
//		else 
//			SetStopTime(0,0,0,0);
//	}

	void StopTimer() {
		started = false;
		stop = true;
	}

	void StopTimerAt(unsigned long days, unsigned long hours, unsigned long minutes, unsigned long seconds) {
		if (TimeCheck(days, hours, minutes, seconds) )
			stop = true;
	}

	void PauseTimer() {
		time = _InternalClock();
		paused = true;
	}

void ResumeTimer() {
    if (remainingSeconds == 0) {
        // Nothing to resume: keep it stopped/paused
        return;
    }
    paused  = false;
    stop    = false;
    started = true;
    time    = _InternalClock();
}

//	void ResumeTimer() { // You can resume the timer if you ever stop it.
//		paused = false;
//		time = _InternalClock();
//	}

	void SetTimer(unsigned long days, unsigned long hours, unsigned long minutes, unsigned long seconds) {
	  // This handles invalid time overflow ie 1(H), 0(M), 120(S) -> 1h, 2m, 0s
		unsigned int sRes = (seconds / 60);
		unsigned int mRes = (minutes / 60);
		
		if(sRes > 0) minutes += sRes;
		if(mRes > 0) hours += mRes;
		if (days > 0) { hours += days * 24; }
	  
		remainingSeconds = (hours * 3600) + (minutes * 60) + (seconds % 60);
		resetClockSeconds = remainingSeconds;
		// stop = false; // #4 
	}

	void SetTimer(unsigned long seconds) {
		// StartTimer(seconds / 3600, (seconds / 3600) / 60, seconds % 60);
		remainingSeconds = seconds;
		resetClockSeconds = remainingSeconds;
		//stop = false; // Pulled #5
	}
	
	void SetStopTime(unsigned long seconds) {
		stopTimeHour = seconds / 3600;
		stopTimeMinute = (seconds / 60) % 60;
		stopTimeSecond = seconds % 60;
		stopTimeTotalTime = seconds;
	}
	
	void SetStopTime(unsigned long days, unsigned long hours, unsigned long minutes, unsigned long seconds) {
		if (days > 0) { hours += days * 24; }
	
		stopTimeHour = hours;
		stopTimeMinute = minutes;
		stopTimeSecond = seconds;
		stopTimeTotalTime = (hours * 3600) + (minutes * 60) + (seconds % 60);
	}

	unsigned long ShowDays() {
		return ShowHours() / 24;
	}
	
	unsigned long ShowHours() {
		return remainingSeconds / 3600;
	}

	unsigned long ShowMinutes() {
		return (remainingSeconds / 60) % 60;
	}

	unsigned long ShowSeconds() {
		return remainingSeconds % 60;
	}
	
	unsigned long rawClock() {
		return remainingSeconds;
	}

	unsigned long ShowMilliSeconds() {
		if (_precision == HIGH)
			if (_type == true)
				return ((_intTime - watch)/ 1000) + 1;
			else 
				return rawClock() * 1000;
		else
			if (_type == true)
				return (_intTime - watch) + 1;
			else 
				return resetClockSeconds - (_intTime - watch);
	}

	unsigned long ShowMicroSeconds() {
		if (_precision == HIGH)
			return (_intTime - watch) + 1;
		else	
			return ((_intTime - watch)/ 1000.0) + 1;
	}
	
	unsigned long ShowTotalSeconds() {
		return remainingSeconds;
	}
	
	unsigned long ShowStopTime() {
		return stopTimeTotalTime;
	}

	boolean TimeHasChanged() {
		return timeFlag;
	}
    
	boolean TimeCheck(unsigned int days, unsigned int hours, unsigned int minutes, unsigned int seconds) { // output true if timer equals requested time or has passed it.
		if (days > 0) { hours += days * 24; }
	
		unsigned long TC = ((hours * 3600) + (minutes * 60) + (seconds % 60));
		if(_type)
			return (remainingSeconds >= TC);
		else  
			return (remainingSeconds <= TC);
	}
	
	boolean TimeCheck() { // output true if timer equals requested time or has passed it.
		if(_type)
			return remainingSeconds >= stopTimeTotalTime;
		else  
			return remainingSeconds <= stopTimeTotalTime;  
	}
	
    private:
	unsigned long duration;
	unsigned long stopTimeHour, stopTimeMinute, stopTimeSecond, stopTimeTotalTime;
	unsigned long watch, _intTime, time;
	unsigned long remainingSeconds, resetClockSeconds;
	boolean reset, stop, paused;
	volatile boolean timeFlag;
	bool _type, _precision;
};

#endif
