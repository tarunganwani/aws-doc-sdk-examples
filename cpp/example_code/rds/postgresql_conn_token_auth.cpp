#include <aws/core/Aws.h>
#include <aws/rds/RDSClient.h>
#include <iostream>

extern "C"{
    #include "libpq-fe.h"
}

namespace aws_rds_demo{

    int ConnectToPostgresSql(const char *connString);
    void ExitNicely(PGconn *conn);

    //Print Utils
    void PrintInfo(const char *msg);
    void PrintError(const char *msg);
    void PrintDebug(const char *msg);

    bool debug = true;

    //Connection string builder
    struct ConnStringBuilder{

        ConnStringBuilder(bool isIAM);
        std::string GetConnString(std::string host, std::string dbname, std::string user, std::string pwd, std::string sslmode, std::string sslrootcert);

    private:
        void InitConnString(bool isIAM);
        void BuildHost(std::string host);
        void BuildUser(std::string user);
        void BuildPassword(std::string password);
        void BuildDbname(std::string dbname);
        void BuildSslMode(std::string sslmode);
        void BuildSslRootCert(std::string sslrootcert);
        void BuildKeyValue(std::string key, std::string val);

        std::string m_strConn;
        bool        m_bIsIAM;
    };
}



/////////////////////////////////////////////////////////////////////////////////////////
//  MAIN
/////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{

    Aws::SDKOptions options;
    Aws::InitAPI(options);

    /////////////////////////////////////////////////////////////////////////////////////////
    //  connection string values
    /////////////////////////////////////////////////////////////////////////////////////////

    auto nlbhost           = "nlb-postgres-public-access-338bdd861265f8e5.elb.ap-south-1.amazonaws.com";
    auto rdshost           = "postgres-instance-private.cg4dwrdzyoeq.ap-south-1.rds.amazonaws.com";
    // auto rdshost           = "postgresql-instance.cg4dwrdzyoeq.ap-south-1.rds.amazonaws.com";    //public host
    auto dbname         = "testdb";
    auto region         = "ap-south-1";
    auto dbport         = 5432;
    auto dbuser         = "db_user_tg";
    auto iamTokenFlag   = true;
    auto sslmode        = "require";
    auto sslrootcert    = "";
    //auto sslmode        = "verify-full";
    //auto sslrootcert    = "/home/tg/aws/rds-ca-2019-root.pem";

    /////////////////////////////////////////////////////////////////////////////////////////
    //  aws rds client - generate auth token
    /////////////////////////////////////////////////////////////////////////////////////////

    const auto config   = Aws::Client::ClientConfiguration("default");
    auto rdsClient      = Aws::RDS::RDSClient(config);
    auto passwordToken  = std::string(rdsClient.GenerateConnectAuthToken(rdshost, region, dbport, dbuser));
    
    /////////////////////////////////////////////////////////////////////////////////////////
    //  connection string builder
    /////////////////////////////////////////////////////////////////////////////////////////

    auto connStr        = aws_rds_demo::ConnStringBuilder(iamTokenFlag).GetConnString(nlbhost, dbname, dbuser, passwordToken, sslmode, sslrootcert);
    aws_rds_demo::PrintDebug("Dumping connection string");
    aws_rds_demo::PrintDebug(connStr.c_str());

    
    /////////////////////////////////////////////////////////////////////////////////////////
    //  connect to postgreSQL
    /////////////////////////////////////////////////////////////////////////////////////////

    auto status = aws_rds_demo::ConnectToPostgresSql(connStr.c_str());

    Aws::ShutdownAPI(options);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////
//  MAIN ENDS
/////////////////////////////////////////////////////////////////////////////////////////


namespace aws_rds_demo {

    /////////////////////////////////////////////////////////////////////////////////////////
    //  ConnectToPostgresSql
    /////////////////////////////////////////////////////////////////////////////////////////

    int ConnectToPostgresSql(const char *connString)
    {
        const char *conninfo;
        PGconn     *conn;
        PGresult   *res;
        int         nFields;
        int         i,
                    j;
        std::string errMsg;

        conninfo = connString;

        /* Make a connection to the database */
        conn = PQconnectdb(conninfo);

        /* Check to see that the backend connection was successfully made */
        if (PQstatus(conn) != CONNECTION_OK)
        {
            errMsg = std::string(PQerrorMessage(conn));
            PrintError(errMsg.c_str());
            ExitNicely(conn);
        }
        PrintInfo("Connection established");

        /* Start a transaction block */
        res = PQexec(conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            errMsg = std::string("BEGIN command failed:");
            errMsg += std::string(PQerrorMessage(conn));
            PrintError(errMsg.c_str());
            PQclear(res);
            ExitNicely(conn);
        }
        PQclear(res);

        PrintInfo("Transaction begun");

        /*
         * Fetch rows from pg_database, the system catalog of databases
         */
        // res = PQexec(conn, "DECLARE myportal CURSOR FOR select * from testtbl2");
        res = PQexec(conn, "call selectproctest('myportal')");
        if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            std::cerr << PQresultStatus(res) << std::endl;
            errMsg = std::string("DECLARE CURSOR failed:");
            errMsg += std::string(PQerrorMessage(conn));
            PrintError(errMsg.c_str());
            PQclear(res);
            ExitNicely(conn);
        }
        PQclear(res);
        PrintInfo("Cursor loaded");

        res = PQexec(conn, "FETCH ALL in myportal");
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            errMsg = std::string("FETCH ALL failed:");
            errMsg += std::string(PQerrorMessage(conn));
            PrintError(errMsg.c_str());
            PQclear(res);
            ExitNicely(conn);
        }
        PrintInfo("Results Fetched");

        /* first, print out the attribute names */
        nFields = PQnfields(res);
        for (i = 0; i < nFields; i++)
            printf("%-15s", PQfname(res, i));
        printf("\n\n");

        /* next, print out the rows */
        for (i = 0; i < PQntuples(res); i++)
        {
            for (j = 0; j < nFields; j++)
                printf("%-15s", PQgetvalue(res, i, j));
            printf("\n");
        }

        PQclear(res);

        /* close the portal ... we don't bother to check for errors ... */
        res = PQexec(conn, "CLOSE myportal");
        PQclear(res);

        /* end the transaction */
        res = PQexec(conn, "END");
        PQclear(res);
        PrintInfo("Transaction End");

        /* close the connection to the database and cleanup */
        PQfinish(conn);

        return 0;
    }

    void ExitNicely(PGconn *conn)
    {
        PQfinish(conn);
        exit(1);
    }

    /////////////////////////////////////////////////////////////////////////////////////////
    //  Print utils
    /////////////////////////////////////////////////////////////////////////////////////////

    void PrintInfo(const char *msg){
        std::cout << "[INFO] " << msg  << std::endl;
    }

    void PrintError(const char *msg){
        std::cerr << "[ERROR] " << msg  << std::endl;
    }

    void PrintDebug(const char *msg){
        if(debug){
            std::cout << "[DEBUG] " << msg  << std::endl;
        }
    }


    /////////////////////////////////////////////////////////////////////////////////////////
    //  ConnStringBuilder
    /////////////////////////////////////////////////////////////////////////////////////////

    ConnStringBuilder::ConnStringBuilder(bool isIAM){
        InitConnString(isIAM);
    }

    std::string ConnStringBuilder::GetConnString(std::string host, std::string dbname, std::string user, std::string pwd, std::string sslmode, std::string sslrootcert){
        BuildHost(host);
        BuildDbname(dbname);
        BuildUser(user);
        BuildPassword(pwd);
        BuildSslMode(sslmode);
        BuildSslRootCert(sslrootcert);
        return m_strConn;
    }

    void ConnStringBuilder::InitConnString(bool isIAM){
        m_strConn   = "";
        m_bIsIAM    = isIAM;
    }

    void ConnStringBuilder::BuildHost(std::string host){
        BuildKeyValue("host", host);
    }

    void ConnStringBuilder::BuildUser(std::string user){
        BuildKeyValue("user", user);
    }

    void ConnStringBuilder::BuildPassword(std::string password){
        BuildKeyValue("password", password);
    }

    void ConnStringBuilder::BuildDbname(std::string dbname){
        BuildKeyValue("dbname", dbname);
    }

    void ConnStringBuilder::BuildSslMode(std::string sslmode){
        BuildKeyValue("sslmode", sslmode);
    }

    void ConnStringBuilder::BuildSslRootCert(std::string sslrootcert){
        BuildKeyValue("sslrootcert", sslrootcert);
    }

    void ConnStringBuilder::BuildKeyValue(std::string key, std::string val){
        m_strConn   += key;
        m_strConn   += "=";
        m_strConn   += val;
        m_strConn   += " ";
    }

}


















