
#include "runner.h"
#include <utilfuncs/utilfuncs.h>
#include <vector>
#include <tuple>
#include <cstring> //memcpy
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <unistd.h>
#include <pwd.h>
#include <sstream>
#include <fstream>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>
#include <atomic>

namespace runner
{

//*********************
extern void showbuf(char **buf);
//*********************


//==================================================================================================
std::string syscom_err;
const std::string get_run_error() { return syscom_err; }
bool set_run_err(const std::string &txt) { syscom_err=(txt.empty()?"unknown error":txt); return false; }

std::string DEF_TERM_EMU=(const char*)"/etc/alternatives/x-terminal-emulator";
std::string DEF_EDITOR=(const char*)"/etc/alternatives/editor";
std::string DEF_GUI_EDITOR=(const char*)"/etc/alternatives/gnome-text-editor";
std::string DEF_WEB_BROWSER=(const char*)"/etc/alternatives/x-www-browser";
///todo:...maybe make above available in header?...

std::string GetDefTermEmu() { return DEF_TERM_EMU; }
std::string GetDefEditor() { return DEF_EDITOR; }
std::string GetDefGuiEditor() { return DEF_GUI_EDITOR; }
std::string GetDefWebBrowser() { return DEF_WEB_BROWSER; }


//void sys_pause(int usec) { std::this_thread::sleep_for(std::chrono::microseconds(usec)); }

//==================================================================================================
//const std::string username()
//{
//	std::string s("");
//	struct passwd *pw=getpwuid(getuid());
//	if (pw) s=pw->pw_name;
//	return s;
//}
//
//const std::string homedir()
//{
//	std::string s("");
//	struct passwd *pw=getpwuid(getuid());
//	if (pw) s=pw->pw_dir;
//	return s;
//}

std::atomic_bool BARS(false);
bool captureBARS() { bool b=BARS.exchange(true); return !b; }
void releaseBARS() { BARS=false; }

void get_env(VSTR &vstr)
{
	char *pc;
	vstr.clear();
	for (int i=0; ((pc=environ[i])!=nullptr); i++) { vstr.push_back(pc); }
}

bool X_is_up() //todo: make failsafe...
{
	Environment env(Environment::ENV_EMPTY);
	Arguments args;
	std::string s;
	args.add("-c","Xorg");
	//example using awk: "ps -ef | grep Xorg | awk '{print $2 " " $8}' | grep Xorg | awk '{print $1}'" ---?why not just print $2 & be done? wtf
	Run("/usr/bin/pgrep", args, env, &s);
	TRIM(s);
	return !seqs(s, "0");
}

void get_safe_env(VSTR &vstr)
{
	char tbuf[4096];
	std::string s;
	vstr.clear();

	//vstr.push_back("PATH=/");
	vstr.push_back("PATH=" _PATH_STDPATH); //-- keep at / - caller must specify exact paths for exec..

	if (getcwd(tbuf, 4096))	{ s="PWD="; s+=tbuf; vstr.push_back(s); } //else vstr.push_back("PWD=.");

	s=getpwuid(geteuid())->pw_shell;
	vstr.push_back("SHELL="+s);
	vstr.push_back("USER="+username());
	vstr.push_back("HOME="+homedir());
	vstr.push_back("LC_ALL=C");
	//vstr.push_back("TEMP=" _PATH_TMP);
	//vstr.push_back("TERM=to do"); -if needed: use get/end/set..usershell() - reads /etc/shells
	//vstr.push_back("DISPLAY=:0.0"); - ?where/how?
	if (X_is_up()) { vstr.push_back("DISPLAY=:0.0"); } //use this as default
	//vstr.push_back("LD_LIBRARY_PATH=to do"); - too specialized?
	//vstr.push_back("TZ=to do"); - ?needed?
	//LANG, $LC_ALL, $LC_...   -- langinfo.h, locale...


}

const std::string make_tmp_file_name()
{
	std::this_thread::sleep_for(std::chrono::microseconds(1)); //ensure different for each call
	std::string s="/tmp/"; s+=get_unique_name(); s+=".tmp";
	return s;
}


//==================================================================================================
Environment::Environment(ENV_TYPE t) { buf=nullptr; reset(t); }

void Environment::clear_buf()	{ if (buf) free(buf); buf=nullptr; }

Environment& Environment::reset(ENV_TYPE t)
{
	//if (t!=ENV_CUSTOM) ?
	 clear(); //=> ENV_EMPTY
	type=t;
	switch(type)
	{
		case ENV_FULL: { get_env(this->vstr); } break;
		case ENV_SAFE: { get_safe_env(this->vstr); } break;
		case ENV_EMPTY: case ENV_CUSTOM:
		default: break;
	}
	return *this;
}

char** Environment::tobuf()
{
	clear_buf();
	size_t pos=0, spos, bufsize, ssize=0;
	char *pc;
	for (auto s:vstr) ssize+=(s.size()+1);
	spos=((vstr.size()+1)*sizeof(char*));
	bufsize=(spos+ssize);
	buf=(char**)malloc(bufsize);
	if (buf)
	{
		for (auto s:vstr)
		{
			pc=(char*)((char*)buf+spos);
			memcpy(pc, s.c_str(), s.size());
			buf[pos++]=pc;
			spos+=s.size();
			*(char*)((char*)buf+spos++)=0;
		}
		buf[pos]=nullptr;//0;
	}
	return buf;
}

void Environment::add_entry(const std::string &nv)
{
	clear_buf();
	bool b=false;
	auto it=vstr.begin();
	while (!b&&(it!=vstr.end())) { if (!(b=seqs((*it), nv))) it++; }
	if (!b) { vstr.push_back(nv); type=ENV_CUSTOM; }
}

void Environment::remove(const std::string &nv)
{
	clear_buf();
	auto it=vstr.begin();
	while (it!=vstr.end()) { if (seqs((*it), nv)) { vstr.erase(it); break; } else it++; }
	type=ENV_CUSTOM;
}

//==================================================================================================
void Arguments::clear_buf() { if (buf) free(buf); buf=nullptr; }

void Arguments::args(const std::string &sargs, bool bAppend) //sep on spaces outside quoted values
{
	Tokens<AToken> tokens;
	if (!bAppend) vargs.clear();
	clear_buf();
	if (tokenize(tokens, sargs)>0)
	{
		std::string arg("");
		for (auto t:tokens)
		{
			if (IsSpace(t.Tok[0])) { if (!arg.empty()) vargs.push_back(arg); arg.clear(); }
			else { arg+=t.Tok; }
		}
		if (!arg.empty()) vargs.push_back(arg);
	}
}

char** Arguments::tobuf(const std::string &sApp)
{
	clear_buf();
	size_t pos=0, spos, bufsize, ssize=(sApp.size()+1);
	char *pc;
	for (auto s:vargs) ssize+=(s.size()+1);
	spos=((vargs.size()+2)*sizeof(char*));
	bufsize=(spos+ssize);
	buf=(char**)malloc(bufsize);
	if (buf)
	{
		pc=(char*)((char*)buf+spos);
		memcpy(pc, sApp.c_str(), sApp.size());
		buf[pos++]=pc;
		spos+=sApp.size();
		*(char*)((char*)buf+spos++)=0;

		for (auto s:vargs)
		{
			pc=(char*)((char*)buf+spos);
			memcpy(pc, s.c_str(), s.size());
			buf[pos++]=pc;
			spos+=s.size();
			*(char*)((char*)buf+spos++)=0;
		}
		buf[pos]=nullptr;//0;
	}
	return buf;
}

inline bool waitpidto(pid_t pid, unsigned int usec=1000, int loopcount=10, int loc_n=0, pid_t loc_i=0, bool loc_b=false) //microseconds
{
	while (loopcount>0)
	{
		loc_i=waitpid(pid, &loc_n, WNOHANG); //0=busy, -1=err, pid=done
		if ((loc_b=(loc_i==pid))||(loc_i==-1)||(WIFEXITED(loc_n)||WIFSIGNALED(loc_n))) break;
		loopcount--;
		kipu(usec);
	}
	return loc_b; //true => pid ended clean (error or terminated otherwise)
}


//==================================================================================================

bool Run(const std::string &App, Arguments Args, Environment Env, std::string *presult, bool Detach)
{
	syscom_err.clear();
	if (!file_exist(App)) return set_run_err(spf(App," - does not exist"));
	std::string fcap=make_tmp_file_name();
	int fncap, oldfnout{}, oldfnerr{};
	auto captureOE=[&]()->bool
		{
			if (((oldfnout=dup(fileno(stdout)))>=0)&&((oldfnerr=dup(fileno(stderr)))>=0)
				&&((fncap=open(fcap.c_str(), O_RDWR|O_CREAT|O_APPEND, 0600))>=0)
				&&(dup2(fncap, fileno(stdout))>=0)&&(dup2(fncap, fileno(stderr))>=0))
			{ close(fncap); return true; }
			return set_run_err(strerror(errno));
		};
	auto releaseOE=[&]()
		{
			fflush(stdout);	dup2(oldfnout, fileno(stdout)); close(oldfnout);
			fflush(stderr); dup2(oldfnerr, fileno(stderr)); close(oldfnerr);
			presult->clear();
			std::ifstream infcap(fcap.c_str());
			if (infcap.good()) { std::stringstream ss; ss << infcap.rdbuf(); (*presult)+=ss.str(); infcap.close(); }
			file_delete(fcap);
		};

	if (presult&&!captureOE()) { return false; }
	pid_t pid;
	if ((pid=fork())<0) { return set_run_err(strerror(errno)); }
	
	if (pid!=0) { if (presult) { waitpid(pid, nullptr, 0); }}
	//if (pid!=0) { if (presult) { waitpidto(pid); }}
	//if (pid!=0) { if (presult) { int tout=10, nst=0; while ((tout>0)&&!waitpid(pid, &nst, WNOHANG)) { if (WIFEXITED(nst)||WIFSIGNALED(nst)) break; tout--; kipu(100); }}}
	//if (pid!=0) { if (presult) { int tout=10, nst=0; while ((tout>0)&&!waitpid(pid, &nst, 0)) { if (WIFEXITED(nst)||WIFSIGNALED(nst)) break; tout--; kipu(100); }}}
	
	else //child..
	{
		if (Detach) setpgid(0,0);
		int r=execve(App.c_str(), Args.tobuf(App), Env.tobuf());
		if (r<0) return set_run_err(strerror(errno)); //todo ..pass to parent
		Args.clear();
		Env.clear();
		exit(0); //kill child
	}
	if (presult) releaseOE();
	return (!errno); //true;
}

bool run_shell(const std::string &cmd, Arguments *pargs, Environment *penv, std::string *presult, bool Detach=false)
{
	//NB: use captureBARS/releaseBARS - see: SysRun & SysCall
	if (!BARS) return set_run_err("invalid call to run_shell");
	syscom_err.clear();
	std::string fcap=make_tmp_file_name();
	int fncap, oldfnout{}, oldfnerr{};
	auto captureOE=[&]()->bool
		{
			if (((oldfnout=dup(fileno(stdout)))>=0)&&((oldfnerr=dup(fileno(stderr)))>=0)
				&&((fncap=open(fcap.c_str(), O_RDWR|O_CREAT|O_APPEND, 0600))>=0)
				&&(dup2(fncap, fileno(stdout))>=0)&&(dup2(fncap, fileno(stderr))>=0))
			{ close(fncap); return true; }
			return set_run_err(strerror(errno));
		};
	auto releaseOE=[&]()
		{
			fflush(stdout);	dup2(oldfnout, fileno(stdout)); close(oldfnout);
			fflush(stderr); dup2(oldfnerr, fileno(stderr)); close(oldfnerr);
			presult->clear();
			std::ifstream infcap(fcap.c_str());
			if (infcap.good()) { std::stringstream ss; ss << infcap.rdbuf(); (*presult)+=ss.str(); infcap.close(); }
			file_delete(fcap);
		};

	if (presult&&!captureOE()) { return false; }
	pid_t pid;
	if ((pid=fork())<0) { return set_run_err(strerror(errno)); }
	if (pid!=0)
	{
		if (Detach) setpgid(0,0);
		
		else if (presult) { waitpid(pid, nullptr, 0); }
		//else if (presult) { waitpidto(pid); } //, 10000, 10, 100); }
//		else if (presult)
//		{
//			int tout=10, nst=0;
//			while ((tout>0)&&!waitpid(pid, &nst, WNOHANG))
//			{
//				if (WIFEXITED(nst)||WIFSIGNALED(nst)) break;
//				tout--;
//				kipm(100);
//			}
//		}
		//else if (presult) { int tout=10, nst=0; while ((tout>0)&&!waitpid(pid, &nst, 0)) { if (WIFEXITED(nst)||WIFSIGNALED(nst)) break; tout--; kipu(100); }}
	}
	else
	{
		std::string parms(cmd); parms+=" "; for (auto s:*pargs) { parms+=s; parms+=" "; }
		execle("/bin/bash", "/bin/bash", "-c", parms.c_str(), nullptr, penv->tobuf());
	}
	if (presult) releaseOE();
	return true;
}

void get_cmdargs(const std::string &cmd, Arguments &A, std::string &sc)
{
	size_t p=cmd.find(' ');
	std::string t;
	if (p!=std::string::npos) { sc=cmd.substr(0,p); t=cmd.substr(p+1); } else sc=cmd;
	A.args(t);
}

bool SysRun(const std::string &cmd, Environment *pE) //fire&forget..
{
	std::string sc;
	std::string rs("");
	Arguments A;
	Environment E(Environment::ENV_SAFE);
	if (pE) E=*pE;
	get_cmdargs(cmd, A, sc);
	while (!captureBARS()) kipu(100);
	bool b=run_shell(sc, &A, &E, &syscom_err, true);
	releaseBARS();
	return b;
}

const std::string SysCall(const std::string &cmd, Environment *pE) //waitforit..
{
	std::string sr(""), sc("");
	Arguments A;
	Environment E(Environment::ENV_SAFE);
	if (pE) E=*pE;
	get_cmdargs(cmd, A, sc);
	while (!captureBARS()) kipu(100);
	run_shell(sc, &A, &E, &sr);
	releaseBARS();
	return sr;
}

int System(const char *command) //does it work? supposed to be ~drop-in~-replace for system() - test it!
{
	if (!command) { set_run_err("System: invalid command"); return 1; }
	return SysRun(command)?1:0;
}

int System(const std::string &command) { return System(command.c_str()); }

//==================================================================================================

typedef std::tuple<size_t, std::string, std::string> ITC; //id, tmpfile, command
const ITC make_itc(size_t i, const std::string &f, const std::string &c) { return std::make_tuple(i, f, c); }
typedef std::vector<ITC> VITC_Type;
typedef VITC_Type::iterator VITCIterator;
VITC_Type VITC;

VITCIterator find_term(size_t ID)
{
	bool b=false;
	auto it=VITC.begin();
	while (!b&&(it!=VITC.end())) { if ((b=(std::get<0>((*it))==ID))) break; else it++; }
	if (b) return it; else return VITC.end();
}

const std::string vitc_file(size_t ID)
{
	auto it=find_term(ID);
	if (it!=VITC.end()) return std::get<1>((*it));
	return "";
}

const std::string vitc_cmd(size_t ID)
{
	auto it=find_term(ID);
	if (it!=VITC.end()) return std::get<2>((*it));
	return "";
}

void remove_temp_id(size_t ID)
{
	auto it=find_term(ID);
	if (it!=VITC.end()) { file_delete(std::get<1>((*it))); VITC.erase(it); }
}

void kill_terminal(size_t ID)
{
	//std::string sid=vitc_cmd(ID);
	//int p=sid.find(' '); if (p!=std::string::npos) sid=sid.substr(0, p);
	//std::string sc=SysCall(spf("/bin/ps -ef | grep \"", sid, "\" | grep tty"));
	//if (!sc.empty()) SysCall(spf("/bin/kill ", stot<int>(sc.substr(0, sc.find(' '))))); //soft-kill
	remove_temp_id(ID);
}

//void cleanup_terminals(void) __attribute__((destructor));
void cleanup_terminals(void)
{
	while (!VITC.empty()) kill_terminal(std::get<0>((*(VITC.begin()))));
	VITC.clear();
}

void do_terminal(const std::string sc, size_t ID, std::string &res)
{
	res=SysCall(sc);
	int tout=50;
	while ((tout>0)&&file_exist(vitc_file(ID))) { tout--; kipu(100); }
	remove_temp_id(ID);
}

size_t temp_command(const std::string &cmd)
{
	std::string sc, sf=make_tmp_file_name(), sh=getpwuid(geteuid())->pw_shell;
	size_t ID=VITC.size();
	std::ofstream ofs(sf);
	if (ofs.good())
	{
		ofs << "#!" << sh  << "\n" << cmd << "\n"; //create file in /tmp to execute the command..
		ofs << "echo \"Press Enter to close...\" ; key=1 ; trap \"unset key\" 1; while [ \"$key\" != \"\" ] ; do read -n1 -s key ; done\n";
		ofs << "rm \"" << sf << "\""; //file must delete itself when done
		ofs.close();
		sc="chmod 755 \""; sc+=sf; sc+="\""; SysRun(sc); //make executable
		VITC.push_back(make_itc(ID,sf,cmd)); //housekeeping cleanup
		return ID;
	}
	return size_t(-1);
}

bool Terminal(const std::string &cmd, bool block)
{
	if (!X_is_up()) return set_run_err("X not running");
	std::string s("");
	size_t ID=temp_command(cmd);
	if (ID!=size_t(-1))
	{
		std::string sc=spf(DEF_TERM_EMU, " -e \"", vitc_file(ID), "\"");
		if (block) do_terminal(sc, ID, s); else SysRun(sc);
	}
	else return set_run_err("cannot create temporaries");
	return true;
}

const std::string SudoCall(const std::string &cmd)
{
	if (has_root_access()) return SysCall(cmd);
	std::string s("");
	if (!geteuid()) s=SysCall(cmd);
	else
	{
		std::string sc, sudocmd, sh, sf=make_tmp_file_name(), sof=make_tmp_file_name();
		sudocmd=spf("sudo -s ", cmd, " > ", sof, " 2>&1");
		std::ofstream ofs(sf);
		if (ofs.good())
		{
			ofs << "#!" << getpwuid(geteuid())->pw_shell << "\n" << sudocmd << "\nrm \"" << sf << "\"\n"; ofs.close();
			sc="chmod 700 \""; sc+=sf; sc+="\""; SysRun(sc);
			sc=(X_is_up())?DEF_TERM_EMU:getpwuid(geteuid())->pw_shell; sc+=" -e \""; sc+=sf; sc+="\"";
			s=SysCall(sc); //todo...fix this - works for now
			while (file_exist(sf)) kipu(100); ///todo ... find a better way to do this
			file_read(sof, s);
			file_delete(sof);
		} //else s="wtf";
	}
	return s;
}

bool SudoRun(const std::string &cmd)
{
	if (has_root_access()) return SysRun(cmd);
	std::string s("");
	if (!geteuid()) return SysRun(cmd);
	else
	{
		std::string sc, sudocmd, sh=getpwuid(geteuid())->pw_shell, sf=make_tmp_file_name();//, sof=make_tmp_file_name();
		sudocmd=spf("sudo -s ", sh, " -c '/usr/bin/nohup ", cmd, " &'\nrm \"", sf, "\"\n");
		std::ofstream ofs(sf);
		if (ofs.good())
		{
			ofs << "#!" << sh << "\n" << sudocmd << "\n"; ofs.close();
			sc="chmod 777 \""; sc+=sf; sc+="\""; SysRun(sc);
			sc=(X_is_up())?DEF_TERM_EMU:sh; sc+=" -e \""; sc+=sf; sc+="\"";
			//bool b=SysRun(sc);
			syscom_err=SysCall(sc);
		return (syscom_err.empty());
			//kipu(1000);
			//return b;
		}
	}
	return false;
}

//-----------------------------------
std::string getwhich(std::string sname)
{
	std::string sr{};
	//static
	 SystemEnvironment SE{};
	std::string sn;
	size_t p;
	sn=sname;
	if ((p=sn.find(' '))!=std::string::npos) sn=sn.substr(0, p);
	TRIM(sn);
	if (sn.empty()) return "";
	if (sn[0]=='/')
	 {
	  if (file_exist(sn))
	   sr=sn;
	    return sr;
	     }
	else if (!SE.empty()||GetSystemEnvironment(SE))
	{
		std::vector<std::string> vp;
		if (splitslist(SE["PATH"], ':', vp)>0)
		{
			std::string sx;
			for (auto s:vp) { sx=path_append(s, sn); if (file_exist(sx)) { sr=sx; break; }}
		}
	}
	//else ... /usr/sbin
	return sr;
}
/*
	std::string sc, se("");
	size_t p;
	if ((p=sname.find(' '))!=std::string::npos) se=sname.substr(0, p); else se=sname;
	TRIM(se);
	if (!se.empty()&&(se[0]!='/')) { sc=spf("which ", se); se=runner::SysCall(sc); TRIM(se); }
	return se;
*/

//---------------------------------------------------------------------------------------------------
void touch_file(std::string f, std::string d) //d=yyyymmddHHMM.SS <--NB: .SS (see: man touch)
{
	if (d.empty()) return;
	std::string scmd{};
	scmd+=spf("touch -t '", d, "' '", f, "' ");
	runner::SysCall(scmd);
}

void touch_tree(std::string sfd, std::string d, DirTree &dtree)
{
	std::string f{};
	touch_file(sfd, d);
	for (auto e:dtree.content) { f=path_append(sfd, e.first); touch_file(f, d); }
	for (auto p:dtree) { f=path_append(sfd, p.first); touch_tree(f, d, p.second); }
}

void Touch(std::string sfd, std::string sdt)
{
	if (sfd.empty()) return;
	auto vdt=[](std::string &s)->bool
			{
				if (s.empty()||(s.size()!=14)) return false;
				int y=stot<uint32_t>(s.substr(0, 4));
				int m=stot<uint32_t>(s.substr(4, 2));
				bool b=is_leap_year(y);
				int d=stot<uint32_t>(s.substr(6, 2));
				if (y<1970) return false;
				if (m>12) return false;
				if ((b&&(m==2)&&(d>29))||(!b&&(m==2)&&(d>28))) return false;
				if (((m==1)||(m==3)||(m==5)||(m==7)||(m==8)||(m==10)||(m==12))&&(d>31)) return false;
				if (((m==4)||(m==6)||(m==9)||(m==11))&&(d>30)) return false;
				if (stot<uint32_t>(s.substr(8, 2))>23) return false;
				if (stot<uint32_t>(s.substr(10, 2))>59) return false;
				if (stot<uint32_t>(s.substr(12, 2))>59) return false;
				std::string t=s;
				s=spf(t.substr(0,12), ".", t.substr(12));
				return true;
			};
	std::string d=((sdt.empty())?ymdhms_stamp():sdt);
	if (!vdt(d)) return;
	if (!isdir(sfd)) { touch_file(sfd, d); return; }
	DirTree dtree{};
	if (dir_read_deep(sfd, dtree)) touch_tree(sfd, d, dtree);
}

std::string getFILEinfo(std::string se)
{
	std::string sret{};
	std::string cmd{};
	cmd=spf("file -bikLr \"", se, "\"");
	sret=SysCall(cmd);
	return sret;
}



} //namespace runner
