#include <errno.h>
#include <string.h>
#include <DPA/SYS/system_error.hpp>
#include <string>

namespace DPA {
  namespace SYS {

    SystemError::SystemError( int errnum )
     : std::runtime_error( strerror(errnum) )
     , number(errnum)
    {}

    SystemError::SystemError()
     : std::runtime_error( strerror(errno) )
     , number(errno)
    {}

    int SystemError::getErrorNumber() const {
      return number;
    }

    int SystemError::getErrno(){
      return errno;
    }

    SystemError::guard::guard( int errnum )
    : number(errnum)
    {}

    SystemError::guard::guard()
    : number(errno)
    {}

    SystemError::guard::~guard() noexcept(false) {
      throw SystemError(number);
    }

  }
}
