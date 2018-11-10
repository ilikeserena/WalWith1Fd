#ifndef PROXYVFS_H
#define PROXYVFS_H

#include "sqlite3.h"

class ProxyVfs
{
 public:
  ProxyVfs();
  ~ProxyVfs();

 private:
  sqlite3_vfs* iUnderlyingVfs;
  sqlite3_vfs iVfs;
  sqlite3_io_methods iIoMethods;
};

#endif  // PROXYVFS_H
