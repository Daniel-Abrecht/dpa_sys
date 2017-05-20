#include <stdexcept>

namespace DPA {
  namespace SYS {

    class SystemError : public std::runtime_error {
      private:
        const int number;

      public:
        SystemError();
        SystemError(int);
        int getErrorNumber() const;
        static int getErrno();

        class guard {
          private:
            const int number;
          public:
            guard();
            guard(int);
            virtual ~guard() noexcept(false);
        };

    };

  }
}
