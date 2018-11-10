#include "ProxyVfs.h"

#include <cassert>
#include <cstdio>
#include <functional>

using namespace std::placeholders;
using std::printf;

namespace {
struct ProxyFile
{
  const struct sqlite3_io_methods* pMethods;
  const char* filename;
  sqlite3_file* underlyingFile;
};

sqlite3_file* underlyingFile(sqlite3_file* file) { return reinterpret_cast<ProxyFile*>(file)->underlyingFile; }
}

ProxyVfs::ProxyVfs() : iUnderlyingVfs(), iVfs(), iIoMethods()
{
  iUnderlyingVfs = sqlite3_vfs_find(nullptr);

  iVfs.iVersion = 3;
  assert(iUnderlyingVfs->iVersion == iVfs.iVersion);
  iVfs.szOsFile = sizeof(ProxyFile);            /* Size of subclassed sqlite3_file */
  iVfs.mxPathname = iUnderlyingVfs->mxPathname; /* Maximum file pathname length */
  iVfs.pNext = nullptr;                         /* Next registered VFS */
  iVfs.zName = "proxyvfs";                      /* Name of this virtual file system */
  iVfs.pAppData = this;                         /* Pointer to application-specific data */
  iVfs.xOpen = [](sqlite3_vfs* vfs, const char* zName, sqlite3_file* file, int flags, int* pOutFlags) {
    ProxyFile* f = reinterpret_cast<ProxyFile*>(file);
    printf("ProxyVfs.xOpen\n");
    f->pMethods = &reinterpret_cast<ProxyVfs*>(vfs->pAppData)->iIoMethods;
    f->filename = zName;
    f->underlyingFile = (sqlite3_file*)calloc(1, vfs->pNext->szOsFile);
    int ret = vfs->pNext->xOpen(vfs->pNext, zName, f->underlyingFile, flags, pOutFlags);
    assert(f->underlyingFile->pMethods->iVersion == f->pMethods->iVersion);
    return ret;
  };

  iVfs.xDelete = [](sqlite3_vfs* vfs, const char* zName, int syncDir) {
    return vfs->pNext->xDelete(vfs->pNext, zName, syncDir);
  };
  iVfs.xAccess = [](sqlite3_vfs* vfs, const char* zName, int flags, int* pResOut) {
    return vfs->pNext->xAccess(vfs->pNext, zName, flags, pResOut);
  };
  iVfs.xFullPathname = [](sqlite3_vfs* vfs, const char* zName, int nOut, char* zOut) {
    return vfs->pNext->xFullPathname(vfs->pNext, zName, nOut, zOut);
  };
  iVfs.xDlOpen = [](sqlite3_vfs* vfs, const char* zFilename) { return vfs->pNext->xDlOpen(vfs->pNext, zFilename); };
  iVfs.xDlError = [](sqlite3_vfs* vfs, int nByte, char* zErrMsg) {
    return vfs->pNext->xDlError(vfs->pNext, nByte, zErrMsg);
  };
  iVfs.xDlSym = [](sqlite3_vfs* vfs, void* p, const char* zSymbol) {
    return vfs->pNext->xDlSym(vfs->pNext, p, zSymbol);
  };
  iVfs.xDlClose = [](sqlite3_vfs* vfs, void* p) { return vfs->pNext->xDlClose(vfs->pNext, p); };
  iVfs.xRandomness = [](sqlite3_vfs* vfs, int nByte, char* zOut) {
    return vfs->pNext->xRandomness(vfs->pNext, nByte, zOut);
  };
  iVfs.xSleep = [](sqlite3_vfs* vfs, int microseconds) { return vfs->pNext->xSleep(vfs->pNext, microseconds); };
  iVfs.xCurrentTime = [](sqlite3_vfs* vfs, double* p) { return vfs->pNext->xCurrentTime(vfs->pNext, p); };
  iVfs.xGetLastError = [](sqlite3_vfs* vfs, int n, char* p) { return vfs->pNext->xGetLastError(vfs->pNext, n, p); };
  /*
  ** The methods above are in version 1 of the sqlite_vfs object
  ** definition.  Those that follow are added in version 2 or later
  */
  iVfs.xCurrentTimeInt64 = [](sqlite3_vfs* vfs, sqlite3_int64* p) {
    return vfs->pNext->xCurrentTimeInt64(vfs->pNext, p);
  };
  /*
  ** The methods above are in versions 1 and 2 of the sqlite_vfs object.
  ** Those below are for version 3 and greater.
  */
  iVfs.xSetSystemCall = [](sqlite3_vfs* vfs, const char* zName, sqlite3_syscall_ptr p) {
    return vfs->pNext->xSetSystemCall(vfs->pNext, zName, p);
  };
  iVfs.xGetSystemCall = [](sqlite3_vfs* vfs, const char* zName) {
    return vfs->pNext->xGetSystemCall(vfs->pNext, zName);
  };
  iVfs.xNextSystemCall = [](sqlite3_vfs* vfs, const char* zName) {
    return vfs->pNext->xNextSystemCall(vfs->pNext, zName);
  };

  iIoMethods.iVersion = 3;
  iIoMethods.xClose = [](sqlite3_file* file) {
    ProxyFile* f = reinterpret_cast<ProxyFile*>(file);
    auto uf = f->underlyingFile;
    int ret = SQLITE_OK;
    if (uf != nullptr)
    {
      ret = uf->pMethods->xClose(uf);
    }
    free(f->underlyingFile);
    f->underlyingFile = nullptr;
    return ret;
  };
  iIoMethods.xRead = [](sqlite3_file* file, void* p, int iAmt, sqlite3_int64 iOfst) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xRead(uf, p, iAmt, iOfst);
  };
  iIoMethods.xWrite = [](sqlite3_file* file, const void* p, int iAmt, sqlite3_int64 iOfst) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xWrite(uf, p, iAmt, iOfst);
  };
  iIoMethods.xTruncate = [](sqlite3_file* file, sqlite3_int64 size) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xTruncate(uf, size);
  };
  iIoMethods.xSync = [](sqlite3_file* file, int flags) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xSync(uf, flags);
  };
  iIoMethods.xFileSize = [](sqlite3_file* file, sqlite3_int64* pSize) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xFileSize(uf, pSize);
  };
  iIoMethods.xLock = [](sqlite3_file* file, int lock) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xLock(uf, lock);
  };
  iIoMethods.xUnlock = [](sqlite3_file* file, int lock) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xUnlock(uf, lock);
  };
  iIoMethods.xCheckReservedLock = [](sqlite3_file* file, int* pResOut) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xCheckReservedLock(uf, pResOut);
  };
  iIoMethods.xFileControl = [](sqlite3_file* file, int op, void* pArg) {
    ProxyFile* f = reinterpret_cast<ProxyFile*>(file);
    printf("ProxyVfs.xFileControl(%s, %d, %p)\n", f->filename, op, pArg);
    auto uf = underlyingFile(file);
    return uf->pMethods->xFileControl(uf, op, pArg);
  };
  iIoMethods.xSectorSize = [](sqlite3_file* file) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xSectorSize(uf);
  };
  iIoMethods.xDeviceCharacteristics = [](sqlite3_file* file) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xDeviceCharacteristics(uf);
  };
  /* Methods above are valid for version 1 */
  iIoMethods.xShmMap = [](sqlite3_file* file, int iPg, int pgsz, int bExtend, void volatile** pp) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xShmMap(uf, iPg, pgsz, bExtend, pp);
  };
  iIoMethods.xShmLock = [](sqlite3_file* file, int offset, int n, int flags) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xShmLock(uf, offset, n, flags);
  };
  iIoMethods.xShmBarrier = [](sqlite3_file* file) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xShmBarrier(uf);
  };
  iIoMethods.xShmUnmap = [](sqlite3_file* file, int deleteFlag) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xShmUnmap(uf, deleteFlag);
  };
  /* Methods above are valid for version 2 */
  iIoMethods.xFetch = [](sqlite3_file* file, sqlite3_int64 iOfst, int iAmt, void** pp) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xFetch(uf, iOfst, iAmt, pp);
  };
  iIoMethods.xUnfetch = [](sqlite3_file* file, sqlite3_int64 iOfst, void* p) {
    auto uf = underlyingFile(file);
    return uf->pMethods->xUnfetch(uf, iOfst, p);
  };
  /* Methods above are valid for version 3 */
  /* Additional methods may be added in future releases */

  sqlite3_vfs_register(&iVfs, 1);
}

ProxyVfs::~ProxyVfs() { sqlite3_vfs_unregister(&iVfs); }
