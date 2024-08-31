#ifndef TEEWORLDS_SIX_TUNE_H
#define TEEWORLDS_SIX_TUNE_H

class CTuneParam
{
	int m_Value;
public:
	void Set(int v) { m_Value = v; }
	int Get() const { return m_Value; }
	CTuneParam &operator = (int v) { m_Value = (int)(v*100.0f); return *this; }
	CTuneParam &operator = (float v) { m_Value = (int)(v*100.0f); return *this; }
	operator float() const { return m_Value/100.0f; }
};

class CTuningParams
{
public:
	CTuningParams()
	{
		const float TicksPerSecond = 50.0f;
		#define MACRO_TUNING_PARAM(Name,ScriptName,Value) m_##Name.Set((int)(Value*100.0f));
		#include "tuning.h"
		#undef MACRO_TUNING_PARAM
	}

	static const char *m_apNames[];

	#define MACRO_TUNING_PARAM(Name,ScriptName,Value) CTuneParam m_##Name;
	#include "tuning.h"
	#undef MACRO_TUNING_PARAM

	static int Num() { return sizeof(CTuningParams)/sizeof(int); }
	bool Set(int Index, float Value);
	bool Set(const char *pName, float Value);
	bool Get(int Index, float *pValue);
	bool Get(const char *pName, float *pValue);
};

#endif // TEEWORLDS_SIX_TUNE_H