//---------------------------------------------------------------------------

#ifndef FtpControlH
#define FtpControlH
//---------------------------------------------------------------------------
#include <Classes.hpp>
//---------------------------------------------------------------------------
class FtpControl : public TThread
{
protected:
	void __fastcall Execute();
public:
	__fastcall FtpControl(bool CreateSuspended);
};
//---------------------------------------------------------------------------
#endif
