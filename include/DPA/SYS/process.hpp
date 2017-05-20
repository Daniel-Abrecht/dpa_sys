#include <vector>
#include <memory>
#include <unistd.h>

namespace DPA {
  namespace SYS {

    class process;
    typedef std::shared_ptr<process> procptr;

    class process {
      private:
        process();
        process(
          const char* program,
          const size_t argc,
          const char*const argv[],
          const size_t fdmapc,
          const int fdmapv[][2]
        );

        volatile pid_t pid;
        procptr* ptr; /* temporary helper */
        int retcode;

        void(*custom_exit_handler)(process*,void*);
        void* custom_exit_handler_param;

        static void init();
        void push_self();
        void child_exit_handler( int retcode ) noexcept;
        static void sigchild_handler( int signum );
        static pid_t waitpid_helper( int p, int* wstate, int flags );
        static void move_close_fds( size_t fdmapc, const int fdmapv[][2], size_t keepfsc, const int keepfdv[] );

        static procptr fork(
          bool preserve_fds,
          const size_t fdmapc,
          const int fdmapv[][2]
        );

      public:

        /* factories */

        static procptr run(
          const char* program,
          const size_t argc,
          const char*const argv[],
          const size_t fdmapc,
          const int fdmapv[][2]
        );

        static procptr run( const char* program );

        static procptr run(
          const char* program,
          const size_t argc,
          const char*const argv[]
        );

        static procptr run(
          const char* program,
          const size_t fdmapc,
          const int fdmapv[][2]
        );

        template<std::size_t N>
        static inline procptr run(
          const char* program,
          const char*const (&args)[N]
        ){
          return run(program,N,args);
        }

        template<std::size_t N>
        static inline procptr run(
          const char* program,
          const int (&fds)[N][2]
        ){
          return run(program,N,fds);
        }

        template<std::size_t N,std::size_t M>
        static inline procptr run(
          const char* program,
          const char*const (&args)[N],
          const int (&fds)[M][2]
        ){
          return run(program,N,args,M,fds);
        }

        static procptr fork( bool preserve_fds=true );

        static procptr fork(
          const size_t fdmapc,
          const int fdmapv[][2]
        );

        template<std::size_t N,std::size_t M>
        static inline procptr fork(
          const int (&fds)[M][2]
        ){
          return fork(N,fds);
        }

        /* Methods */

        static void waitAll();

        bool isRunning();
        void wait();
        void kill( bool force=false );
        void quit();
        void pause();
        void resume();
        void signal( int signal );

        template <typename T>
        inline void setExitHandler( void handler(process*,T*) noexcept, T* param ){
          this->custom_exit_handler = (void(*)(process*,void*))handler;
          this->custom_exit_handler_param = (void*)param;
        }

    };

  }
}
