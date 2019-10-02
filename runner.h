#ifndef _runner_runner_h_
#define _runner_runner_h_

#include <string>
#include <vector>
#include <stdexcept> //for inline .. Sudo(..) below

namespace runner
{
	
typedef std::vector<std::string> VSTR;

void get_env(VSTR &vstr);
void get_safe_env(VSTR &vstr);

std::string GetDefTermEmu();
std::string GetDefEditor();
std::string GetDefGuiEditor();
std::string GetDefWebBrowser();

//==================================================================================================
struct Environment
{
	typedef VSTR::iterator iterator;
	typedef VSTR::reverse_iterator reverse_iterator;
	enum ENV_TYPE { ENV_EMPTY=0, ENV_FULL, ENV_SAFE, ENV_CUSTOM, };

private:
	VSTR vstr;
	ENV_TYPE type; //use is..()-funcs
	char **buf;
	void clear_buf();
	void add_entry(const std::string &nv);

public:

	void clear()		{ vstr.clear(); clear_buf(); type=ENV_EMPTY; }
	bool isempty()	const { return vstr.empty(); }
	bool isfull()	const { return (type==ENV_FULL); }
	bool issafe()	const { return (type==ENV_SAFE); }
	bool iscustom()	const { return (type==ENV_CUSTOM); }
	~Environment()						{ clear(); }
	Environment(ENV_TYPE t=ENV_SAFE);
	Environment(const Environment &E)	{ buf=nullptr; *this=E; }
	Environment& operator=(const Environment &E)		{ type=E.type; vstr=E.vstr; return *this; }
	Environment& reset(ENV_TYPE t=ENV_EMPTY);
	iterator begin()			{ return vstr.begin(); }
	iterator end()				{ return vstr.end(); }
	reverse_iterator rbegin()	{ return vstr.rbegin(); }
	reverse_iterator rend()		{ return vstr.rend(); }
	char** tobuf();
	///NB: add()/remove() invalidates buf/tobuf()! **********
	template<typename...T> void add() { type=ENV_CUSTOM; clear_buf(); }
	template<typename H=std::string, typename...T> void add(H h,T...t) { add_entry(h); add(t...); }
	void remove(const std::string &nv);
};

//==================================================================================================
struct Arguments
{
	typedef VSTR::iterator iterator;
	typedef VSTR::reverse_iterator reverse_iterator;

private:
	VSTR vargs;
	char **buf;
	void clear_buf();

public:
	void clear()			{ vargs.clear(); clear_buf(); }
	bool isempty()	  const { return vargs.empty(); }
	template<typename...T> Arguments()	{ buf=nullptr; clear(); }
	template<typename H=std::string, typename...T> Arguments(H h, T...t) { buf=nullptr; clear(); add(h, t...); }
	~Arguments() { clear(); }
	Arguments(const Arguments &A) { buf=nullptr; *this=A; }
	Arguments& operator=(const Arguments &A) { vargs=A.vargs; return *this; }
	iterator begin()			{ return vargs.begin(); }
	iterator end()				{ return vargs.end(); }
	reverse_iterator rbegin()	{ return vargs.rbegin(); }
	reverse_iterator rend()		{ return vargs.rend(); }
	char** tobuf(const std::string &sApp); //prep for exec..
	///***** NB: add()/args() invalidates buf/tobuf()! *****
	template<typename...T> Arguments& add() { clear_buf(); return *this; }
	template<typename H=std::string, typename...T> Arguments& add(H h,T...t) { vargs.push_back(h); return add(t...); }
	void args(const std::string &sargs, bool bAppend=false); //string containing all args as if for cli
	void prepend(const std::string &arg) { vargs.insert(vargs.begin(), arg); }
	void remove0() { vargs.erase(vargs.begin()); }
};

const std::string get_run_error();

bool Run(const std::string &App, Arguments Args=Arguments(), Environment Env=Environment(), std::string *presult=nullptr, bool Detach=true); //default detach to survive parent
inline bool RunAttached(const std::string &App, Arguments &Args, Environment &Env, std::string *presult) { return Run(App, Args, Env, presult, false); } //die with parent
bool SysRun(const std::string &cmd, Environment *pE=nullptr); //'fire&forget' (shell-call with no return-value)
const std::string SysCall(const std::string &cmd, Environment *pE=nullptr); //'waitforit' (shell-call returns call-output)
int System(const char *command); //~almost~ drop-in replacement for system()
int System(const std::string &command); //alias
bool Terminal(const std::string &cmd, bool block=false); //show output in terminal
inline std::string Sudo(const std::string &cmd) { throw std::logic_error("use SudoCall"); return ""; }
const std::string SudoCall(const std::string &cmd); //sudo shell-call
bool SudoRun(const std::string &cmd); //runs app with sudo

/*
just lookathat! all these runners and no killing...wtf?
*/


//-----------------------------------
void cleanup_terminals(void);

//-----------------------------------
std::string getwhich(std::string sname); //replacement for SysCall("which ..") - searches PATH for sname

//Touch: sfd=file:touch | dir(must exist):deep recursive touch,
//dst=(""=>now) | ("yyyymmddHHMMSS"); does nothing if sdt is invalid
void Touch(std::string sfd, std::string sdt="");

std::string getFILEinfo(std::string se); //uses system "file ..." command

} //namespace runner

#endif
