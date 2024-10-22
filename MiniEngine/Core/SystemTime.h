//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//
// Contains classes needed to time executing code.
//

#pragma once

#include "GraphicsCore.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>

class SystemTime
{
public:

    // Query the performance counter frequency
    static void Initialize( void );

    // Query the current value of the performance counter
    static int64_t GetCurrentTick( void );

    static void BusyLoopSleep( float SleepTime );

    static inline double TicksToSeconds( int64_t TickCount )
    {
        return TickCount * sm_CpuTickDelta;
    }

    static inline double TicksToMillisecs( int64_t TickCount )
    {
        return TickCount * sm_CpuTickDelta * 1000.0;
    }

    static inline double TimeBetweenTicks( int64_t tick1, int64_t tick2 )
    {
        return TicksToSeconds(tick2 - tick1);
    }

private:

    // The amount of time that elapses between ticks of the performance counter
    static double sm_CpuTickDelta;
};


class CpuTimer
{
public:
#define MAX_TICKS 120
#define WRITE_FILE_TICKS 3000

#ifdef _DEBUG
	
// Force logging to file off for day to day run sessions.
// Overrides logging for all timers with false.
// Logging options should not be forced on non Debug builds.
//#define FORCE_NO_LOG
	
#endif
	
    CpuTimer(bool logging = false, std::string name = "UNKNOWN")
    {
#ifdef FORCE_NO_LOG
        m_logging = false;
#else
        m_logging = logging;
#endif
        Initialize(name);
    }
	
    ~CpuTimer()
    {
		if(m_outputFile.is_open())
		{
            for (int i = 0; i < m_pastTicks.size(); ++i)
            {
                m_outputFile << m_pastTicks[i].first << "," << SystemTime::TicksToMillisecs(m_pastTicks[i].second) << "\n";
            }

            m_outputFile.close();
		}
    }

    void Start()
    {
        if (m_StartTick == 0ll)
            m_StartTick = SystemTime::GetCurrentTick();
    }

    void Stop()
    {
        if (m_StartTick != 0ll)
        {
            m_ElapsedTicks += SystemTime::GetCurrentTick() - m_StartTick;
            m_StartTick = 0ll;
        }
    }
	
    void Reset()
    {
        m_pastTicks.push_back({ Graphics::GetFrameCount(), m_ElapsedTicks });

        if(m_ElapsedTicks > m_longestTick)
        {
            m_longestTick = m_ElapsedTicks;
        }
        else if(m_ElapsedTicks < m_shortestTick)
        {
            m_shortestTick = m_ElapsedTicks;
        }
    	
        m_ElapsedTicks = 0ll;
        m_StartTick = 0ll;
    }

    double GetTime() const
    {
        return SystemTime::TicksToSeconds(m_ElapsedTicks);
    }

	double GetTimeMilliseconds() const
    {
        return SystemTime::TicksToMillisecs(m_ElapsedTicks);
    }

	double GetAverageTimeMilliseconds() const
    {
        int64_t avg = 0;
    	
        for (int i = m_pastTicks.size() - MAX_TICKS; i < m_pastTicks.size(); i++)
        {
            avg += m_pastTicks[i].second;
	    }

        avg /= MAX_TICKS;

        return SystemTime::TicksToMillisecs(avg);
    }

	double GetLongestTickToMilliseconds() const
    {
        return SystemTime::TicksToMillisecs(m_longestTick);
    }

    double GetShortestTickToMilliseconds() const
    {
        return SystemTime::TicksToMillisecs(m_shortestTick);
    }

	void WriteTicksToFile()
    {
        std::ofstream m_outputFile;
	    
    }
	
private:

    void Initialize(std::string name)
    {
        m_StartTick = 0ll;
        m_ElapsedTicks = 0ll;

        m_pastTicks.reserve(WRITE_FILE_TICKS);

#if _DEBUG
        const auto filename = "logs/debug_log_" + currentDateTime() + "_" + name + ".txt";
#else
        const auto filename = "logs/log_" + currentDateTime() + "_" + name + ".txt";
#endif
        char buf[1024];

        if(m_logging)
    	{
		    GetCurrentDirectoryA(1024, buf);

        	printf_s("Wrote file to: %s\n", filename.c_str());
        	printf_s("Dir: %s\n", buf);

        	m_outputFile = std::ofstream(filename);

        	if (m_outputFile.fail()) {
        		strerror_s(buf, 1024, errno);
        		std::cerr << "Open failed: " << buf << '\n';
        	}
	    }
    }
	
	// Stolen from https://stackoverflow.com/questions/997946/how-to-get-current-time-and-date-in-c/10467633#10467633
    // Get current date/time, format is YYYYMMDD_HHmmss
    static const std::string currentDateTime() {
        time_t     now = time(nullptr);
        struct tm  tstruct;
        char       buf[80];
        tstruct = *localtime(&now);
        // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
        // for more information about date/time format
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tstruct);

        return buf;
    }
	
    std::ofstream m_outputFile;

    bool m_logging = false;
	
    int64_t m_StartTick;
    int64_t m_ElapsedTicks;

    int64_t m_shortestTick = INT64_MAX;
    int64_t m_longestTick = INT64_MIN;
	
    UINT m_writesCount = 0;
    std::vector<std::pair<uint64_t, int64_t>> m_pastTicks;
};
