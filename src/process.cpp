#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <DPA/SYS/process.hpp>
#include <DPA/SYS/system_error.hpp>

namespace DPA {
  namespace SYS {

    class sigchild_guard {
      private:
        sigset_t orig_mask;
        bool ok;
      public:
        sigchild_guard():ok(0){
          sigset_t mask;
          if( sigemptyset(&mask) == -1 )
            throw SystemError();
          if( sigaddset( &mask, SIGCHLD ) == -1 )
            throw SystemError();
          if( sigprocmask( SIG_BLOCK, &mask, &orig_mask ) == -1 )
            throw SystemError();
          ok = 1;
        }
        virtual ~sigchild_guard() noexcept(false) {
          if( !ok ) return;
          if( sigprocmask( SIG_SETMASK, &orig_mask, 0 ) == -1 )
            throw SystemError();
        }
    };

    std::vector<procptr> children;
    bool initialized;

    void process::init(){
      if( initialized )
        return;
      static struct sigaction action;
      action.sa_handler = &process::sigchild_handler;
      if( sigaction( SIGCHLD, &action, 0 ) == -1 )
        throw SystemError();
      initialized = true;
    }

    /* factory */

    procptr process::run(
      const char* program,
      const size_t argc,
      const char*const argv[],
      const size_t fdmapc,
      const int fdmapv[][2]
    ){
      procptr ptr( *(new process( program, argc, argv, fdmapc, fdmapv ))->ptr );
      delete ptr->ptr;
      return ptr;
    }

    procptr process::run(
      const char* program
    ){
      return run( program, 0, 0, 0, 0 );
    }

    procptr process::run(
      const char* program,
      const size_t argc,
      const char*const argv[]
    ){
      return run( program, argc, argv, 0, 0 );
    }

    procptr process::run(
      const char* program,
      const size_t fdmapc,
      const int fdmapv[][2]
    ){
      return run( program, 0, 0, fdmapc, fdmapv );
    }

    /* real constructor, only called by factory methods */

    process::process()
     : pid(0), ptr(0), retcode(0), custom_exit_handler(0)
    {}

    process::process(
      const char* program,
      const size_t argc,
      const char*const argv[],
      const size_t fdmapc,
      const int fdmapv[][2]
    ) : pid(0), ptr(0), retcode(0), custom_exit_handler(0)
    {

      /* Validating arguments */

      if( !program )
        throw std::invalid_argument("process::process: program was null");
      if( argc && !argv )
        throw std::invalid_argument("process::process: argc nonzero but argv is null");
      if( fdmapc && !fdmapv )
        throw std::invalid_argument("process::process: fdmapc nonzero but fdmapv is null");

      for( size_t i=0; i<fdmapc; i++ )
        for( size_t j=i+1; j<fdmapc; j++ )
          if( fdmapv[i][1] == fdmapv[j][1] )
            std::invalid_argument("process::process: fdmapv: dublicate target fds");

      /* arguments seam fine, let's go */

      init();

      this->ptr = new procptr(this);

      try {
        sigchild_guard lock;
        children.push_back(*this->ptr);
      } catch( ... ){
        delete this->ptr;
        throw;
      }

      bool isChild = false;
      char strnum[sizeof(int)*2+1] = {0};
      int errorfds[2] = {-1,-1};

      try {

        if( pipe(errorfds) == -1 )
          throw SystemError();

        const char** arguments = new const char*[argc+2];
        for( size_t i=0; i<argc; i++ )
          arguments[i+1] = argv[i] ? argv[i] : "";
        arguments[0] = program;
        arguments[argc+1] = 0;

        if( fcntl( errorfds[1], F_SETFD, FD_CLOEXEC ) == -1 ){
          SystemError::guard guard;
          close(errorfds[0]);
          close(errorfds[1]);
        }

        pid_t pid;
        {
          sigchild_guard lock;
          pid = ::fork();
          if( pid != -1 )
            this->pid = pid;
          if( !pid )
            isChild = true;
        }

        if( pid == -1 ){
          SystemError::guard guard;
          close(errorfds[0]);
          close(errorfds[1]);
        }

        if( isChild ){
          close(errorfds[0]);
          move_close_fds( fdmapc, fdmapv, 2, errorfds );
          execv(program,(char**)arguments);
          throw SystemError();
        }

        close(errorfds[1]);

        int ret;
        do {
          ret = read(errorfds[0],strnum,sizeof(strnum));
        } while( ret == -1 && SystemError::getErrno() == EINTR );
        if( ret == -1 ){
          SystemError::guard guard;
          close(errorfds[0]);
        }

        close(errorfds[0]);

        if( ret ){
          unsigned int error;
          std::sscanf(strnum,"%x",&error);
          throw SystemError(error);
        }

      } catch( ... ) {
        if( isChild ){
          std::exception_ptr exception = std::current_exception();
          try {
            std::rethrow_exception(exception);
          } catch( const SystemError& syserr ) {
            int count = std::snprintf(strnum,sizeof(strnum),"%x",syserr.getErrorNumber());
            write(errorfds[1],strnum,count);
            close(errorfds[1]);
          } catch( ... ) {
            // this will never happen, leave it here just in case
          }
          std::abort();
        }else{
          pid = 0;
          {
            sigchild_guard lock;
            children.pop_back();
          }
          delete this->ptr;
          throw;
        }
      }

    }

    /* fork */

    procptr process::fork( bool preserve_fds ){
      return process::fork( preserve_fds, 0, 0 );
    }

    procptr process::fork(
      const size_t fdmapc,
      const int fdmapv[][2]
    ){
      return process::fork( false, fdmapc, fdmapv );
    }

    procptr process::fork(
      bool preserve_fds,
      const size_t fdmapc,
      const int fdmapv[][2]
    ){
      if( fdmapc && !fdmapv )
        throw std::invalid_argument("process::process: fdmapc nonzero but fdmapv is null");

      for( size_t i=0; i<fdmapc; i++ )
        for( size_t j=i+1; j<fdmapc; j++ )
          if( fdmapv[i][1] == fdmapv[j][1] )
            std::invalid_argument("process::process: fdmapv: dublicate target fds");

      procptr child = procptr(new process());
      {
        sigchild_guard lock;
        children.push_back(child);
      }

      bool isChild = false;
      char strnum[sizeof(int)*2+1] = {0};
      int errorfds[2] = {-1,-1};

      try {

        if( pipe(errorfds) == -1 )
          throw SystemError();

        pid_t pid;
        {
          sigchild_guard lock;
          pid = ::fork();
          if( pid != -1 )
            child->pid = pid;
          if( !pid )
            isChild = true;
        }
        if( pid == -1 )
          throw SystemError();

        if( isChild ){

          close(errorfds[0]);
          {
            sigchild_guard lock;
            children.clear();
            initialized = false;
            process::init();
          }
          if( !preserve_fds )
            move_close_fds( fdmapc, fdmapv, 2, errorfds );
          close(errorfds[1]);
          return procptr();

        }else{

          close(errorfds[1]);

          int ret;
          do {
            ret = read(errorfds[0],strnum,sizeof(strnum));
          } while( ret == -1 && SystemError::getErrno() == EINTR );
          if( ret == -1 ){
            SystemError::guard guard;
            close(errorfds[0]);
          }

          close(errorfds[0]);

          if( ret ){
            unsigned int error;
            std::sscanf(strnum,"%x",&error);
            throw SystemError(error);
          }

        }

      } catch( ... ) {
        if(isChild){
          std::exception_ptr exception = std::current_exception();
          try {
            std::rethrow_exception(exception);
          } catch( const SystemError& syserr ) {
            int count = std::snprintf(strnum,sizeof(strnum),"%x",syserr.getErrorNumber());
            write(errorfds[1],strnum,count);
            close(errorfds[1]);
          } catch( ... ) {
            // this will never happen, leave it here just in case
          }
          std::abort();
        }else{
          {
            sigchild_guard lock;
            children.pop_back();
          }
          throw;
        }
      }
      return child;
    }

    /* other stuff */

    void process::move_close_fds(
      size_t fdmapc,
      const int fdmapv[][2],
      size_t keepfdc,
      const int keepfdv[]
    ){
      if(!(fdmapc+keepfdc)){
        int openfdmax = sysconf(_SC_OPEN_MAX);
        for( int fd=0; fd<openfdmax; fd++ )
          close(fd);
        return;
      }

      int maxfd = 0;
      // I'm only using malloc because this datatype can't be created with new...
      int (*fdm)[][2] = (int(*)[][2])std::malloc(sizeof(int)*2*(fdmapc+keepfdc));
      if( !fdm )
        throw SystemError();
      std::memcpy(fdm,fdmapv,sizeof(int)*2*fdmapc);

      for( size_t i=0; i<keepfdc; i++ )
        (*fdm)[i+fdmapc][0] = (*fdm)[i+fdmapc][1] = keepfdv[i];
      fdmapc += keepfdc;

      try {

        /* Moving filedescriptors arround */
        for( size_t i=0; i<fdmapc; i++ ){
          if( (*fdm)[i][0] >= 0 && (*fdm)[i][0] != (*fdm)[i][1] ){
            int swapout;
            while(
              ( swapout = dup((*fdm)[i][1]) ) == -1 &&
              ( SystemError::getErrno() == EINTR || SystemError::getErrno() == EBUSY )
            );
            if( swapout == -1 && SystemError::getErrno() == EMFILE )
              throw SystemError();
            if( swapout != -1 ){
              bool used = false;
              for( size_t j=i+1; j<fdmapc; j++ )
                if( (*fdm)[j][0] == swapout ){
                  used = true;
                  (*fdm)[j][0] = swapout;
                }
              if(!used)
                close(swapout);
            }
            close((*fdm)[i][1]);
            int ret;
            while(
              ( ret = dup2( (*fdm)[i][0], (*fdm)[i][1] ) ) == -1 &&
              ( SystemError::getErrno() == EINTR || SystemError::getErrno() == EBUSY )
            );
            if( ret == -1 && SystemError::getErrno() == EMFILE )
              throw SystemError();
          }
          if( maxfd < (*fdm)[i][1] )
            maxfd = (*fdm)[i][1];
        }
        /* closing remaining filedescriptors */
        int openfdmax = sysconf(_SC_OPEN_MAX);
        for( int fd=maxfd+1; fd<openfdmax; fd++ )
          close(fd);
        for( int fd=0; fd<=maxfd; fd++ ){
          for( size_t i=0; i<fdmapc; i++ )
            if( (*fdm)[i][1] == fd )
              goto skip;
          close(fd);
          skip:;
        }

      } catch( ... ) {
        std::free(fdm);
        throw;
      }

    }

    void process::sigchild_handler( int signum ){
      (void)signum;
      while( waitpid_helper( -1, 0, WNOHANG ) > 0 );
    }

    pid_t process::waitpid_helper( int p, int* wstate, int flags ){
      int retcode;
      pid_t pid = waitpid( p, &retcode, flags );
      if( wstate )
        *wstate = retcode;
      if( pid > 0 ){
        std::vector<procptr>::iterator it = std::find_if(
          children.begin(), children.end(),
          [pid]( procptr& p ){ return p->pid == pid; }
        );
        if( it == children.end() )
          return pid;
        children.erase(it);
        (*it)->child_exit_handler( retcode );
      }
      return pid;
    }

    void process::child_exit_handler( int retcode ) noexcept {
      this->retcode = retcode;
      pid = 0;
      if( this->custom_exit_handler )
        (*this->custom_exit_handler)(this,this->custom_exit_handler_param);
    }

    bool process::isRunning(){
      return !!pid;
    }

    void process::wait(){
      int pid;
      while( ( pid = this->pid ) && waitpid_helper( pid, 0, 0 ) );
    }

    void process::kill( bool force ){
      int pid = this->pid;
      if( !pid ) return;
      if( ::kill( pid, force ? SIGTERM : SIGKILL ) == -1 ){
        SystemError error;
        if( error.getErrorNumber() != ESRCH )
          throw error;
      }
    }

    void process::quit(){
      int pid = this->pid;
      if( !pid ) return;
      if( ::kill( pid, SIGQUIT ) == -1 ){
        SystemError error;
        if( error.getErrorNumber() != ESRCH )
          throw error;
      }
    }

    void process::signal( int signal ){
      switch( signal ){
        case SIGKILL:
          throw std::invalid_argument("Use process::kill() for SIGTERM");
        case SIGTERM:
          throw std::invalid_argument("Use process::kill(true) for SIGKILL");
        case SIGSTOP:
          throw std::invalid_argument("Use process::quit() for SIGQUIT");
      }
      int pid = this->pid;
      if( !pid )
        throw SystemError(ESRCH);
      if( ::kill( pid, signal ) == -1 ){
        SystemError error;
        if( error.getErrorNumber() == EINVAL ){
          throw std::invalid_argument("An invalid signal was specified");
        }else{
          throw error;
        }
      }
    }

    void process::pause(){
      process::signal( SIGSTOP );
    }

    void process::resume(){
      process::signal( SIGCONT );
    }

    void process::waitAll(){
      procptr current;
      while( true ){
        {
          sigchild_guard lock;
          if( children.empty() )
            break;
          current = children.back();
        }
        current->wait();
      }
    }

  }
}
