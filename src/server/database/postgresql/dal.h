//
// Database Abstraction Layer (sqlite3)
// Stefano Angeleri
// 05/11/10
//

#ifndef __DAL_H__
#define __DAL_H__

#include "iserver/idal.h"

#include <csutil/scf.h>
#include <csutil/scf_implementation.h>
#include <csutil/threading/thread.h>

#include "iutil/comp.h"

#include "net/netbase.h"  // Make sure that winsock is included.

#include <libpq-fe.h>

#include <csutil/csstring.h>
#include "util/stringarray.h"
#include "util/dbprofile.h"

using namespace CS::Threading;

struct iObjectRegistry;

#ifdef USE_DELAY_QUERY
#define THREADED_BUFFER_SIZE 300
class DelayedQueryManager : public CS::Threading::Runnable
{
public:
    DelayedQueryManager(const char *host, unsigned int port, const char *database,
                              const char *user, const char *pwd);

    virtual void Run ();
    void Push(csString query);
    void Stop();
private:
    //pqxx::connection* m_conn;
    csString arr[THREADED_BUFFER_SIZE];
    size_t start, end;
    CS::Threading::Mutex mutex;
    CS::Threading::RecursiveMutex mutexArray;
    CS::Threading::Condition datacondition;
    bool m_Close;
    psDBProfiles profs;
    csString m_host;
    unsigned int m_port;
    csString m_db;
    csString m_user;
    csString m_pwd;
};
#endif

class psMysqlConnection : public scfImplementation2<psMysqlConnection, iComponent, iDataConnection>
{
protected:
    PGconn* conn; //Points to mydb after a successfull connection to the db
    uint64 lastRow;
    csString lastquery;
    iObjectRegistry *objectReg;
    psDBProfiles profs;
    csString profileDump;
    LogCSV* logcsv;

public:
    psMysqlConnection(iBase *iParent);
    virtual ~psMysqlConnection();

    bool Initialize (iObjectRegistry *objectreg);
    bool Initialize(const char *host, unsigned int port, const char *database, 
                    const char *user, const char *pwd, LogCSV* logcsv);
    virtual bool Close();

    int IsValid(void);

    /** Escapes a string to safely insert it into the database.
     *  @param to Where the resulting escaped string will be placed.
     *  @param from The string that we want to escape.
     */
    void Escape(csString& to, const char *from);
    
    iResultSet *Select(const char *sql,...);
    int SelectSingleNumber(const char *sql, ...);
    unsigned long Command(const char *sql,...);
    unsigned long CommandPump(const char *sql,...);

    uint64 GenericInsertWithID(const char *table,const char **fieldnames,psStringArray& fieldvalues);
    bool GenericUpdateWithID(const char *table,const char *idfield,const char *id,const char **fieldnames,psStringArray& fieldvalues);
    bool GenericUpdateWithID(const char *table,const char *idfield,const char *id,psStringArray& fields);

    const char *GetLastError(void);
    const char *GetLastQuery(void)
    {
        return lastquery;
    };
    uint64 GetLastInsertID();
    
    const char *uint64tostring(uint64 value,csString& recv);

    virtual const char* DumpProfile();
    virtual void ResetProfile();
    
    iRecord* NewUpdatePreparedStatement(const char* table, const char* idfield, unsigned int count, const char* file, unsigned int line);
    iRecord* NewInsertPreparedStatement(const char* table, unsigned int count, const char* file, unsigned int line);

#ifdef USE_DELAY_QUERY    
    csRef<DelayedQueryManager> dqm;
    csRef<Thread> dqmThread;
#endif    
};


class psResultRow : public iResultRow
{
protected:
    int rowNum;
    PGresult *rs;
    int max;

public:
    psResultRow()
    {
        rowNum = 0;
        rs = NULL;
    };

    void SetMaxFields(int fields)  {    max = fields;   };
    void SetResultSet(void* resultsettoken);

    int Fetch(int row);

    const char *operator[](int whichfield);
    const char *operator[](const char *fieldname);

    int GetInt(int whichfield);
    int GetInt(const char *fieldname);

    unsigned long GetUInt32(int whichfield);
    unsigned long GetUInt32(const char *fieldname);

    float GetFloat(int whichfield);
    float GetFloat(const char *fieldname);

    uint64 GetUInt64(int whichfield);
    uint64 GetUInt64(const char *fieldname);

    uint64 stringtouint64(const char *stringbuf);
    const char *uint64tostring(uint64 value,char *stringbuf,int buflen);
};

class psResultSet : public iResultSet
{
protected:
    PGresult *rs;
    unsigned long rows, fields, current;
    psResultRow  row;

public:
    psResultSet(PGresult *res);
    virtual ~psResultSet();

    void Release(void) { delete this; };

    iResultRow& operator[](unsigned long whichrow);

    unsigned long Count(void) { return rows; };
};

class dbRecord : public iRecord
{
protected:
    enum FIELDTYPE { SQL_TYPE_FLOAT, SQL_TYPE_INT, SQL_TYPE_STRING, SQL_TYPE_NULL };

    typedef struct
    {
        float fValue;
        int iValue;
        csString sValue;
        FIELDTYPE type;
    } StatField;
    const char* table;
    const char* idfield;
    
    PGconn *conn;
    
    psStringArray command;
    bool prepared;
    
    csString stmt;
    
    unsigned int index;
    unsigned int count;
    
    // Useful for debugging
    LogCSV* logcsv;
    const char* file;
    unsigned int line;
    
    StatField* temp;
    
    virtual void AddToStatement(const char* fname) = 0;
    
    virtual void SetID(uint32 uid) = 0;
    
public:
    dbRecord(PGconn* db, const char* Table, const char* Idfield, unsigned int count, LogCSV* logcsv, const char* file, unsigned int line)
    {
        conn = db;
        table = Table;
        idfield = Idfield;
        temp = new StatField[count];
        index = 0;
        this->count = count;
        this->logcsv = logcsv;
        this->file = file;
        this->line = line;
        
        //stmt = NULL;
        prepared = false;
    }
    
    virtual ~dbRecord()
    {
        //sqlite3_finalize(stmt);
        delete[] temp;
    }
    
    void Reset()
    {
        index = 0;
        command.Empty(); //clears the command array to avoid restarting from a wrong position.
        //if(stmt)
         //   sqlite3_reset(stmt);        
    }

    
    void AddField(const char* fname, float fValue);
    
    void AddField(const char* fname, int iValue);
    
    void AddField(const char* fname, unsigned int uiValue);
    
    void AddField(const char* fname, unsigned short usValue);

    void AddField(const char* fname, const char* sValue);

    void AddFieldNull(const char* fname);
    
    virtual bool Prepare() = 0;

    virtual bool Execute(uint32 uid);
};

class dbInsert : public dbRecord
{
    virtual void AddToStatement(const char* fname)
    {
        if(!prepared)
            command.Push(fname);
    }
    
    virtual void SetID(uint32 uid)  {  };
    
public:
    dbInsert(PGconn* db, const char* Table, unsigned int count, LogCSV* logcsv, const char* file, unsigned int line)
    : dbRecord(db, Table, "", count, logcsv, file, line) { }
    
    virtual bool Prepare();
};

class dbUpdate : public dbRecord
{
    virtual void AddToStatement(const char* fname)
    {
        if(!prepared)
            command.FormatPush("%s = ?", fname);
    }
    
    virtual void SetID(uint32 uid) 
    {
        temp[index].iValue = uid;
        temp[index].type = SQL_TYPE_INT;
        index++;
    }
public:
    dbUpdate(PGconn* db, const char* Table, const char* Idfield, unsigned int count, LogCSV* logcsv, const char* file, unsigned int line)
    : dbRecord(db, Table, Idfield, count, logcsv, file, line) { }
    virtual bool Prepare();

};
    
#endif

