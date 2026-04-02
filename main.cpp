#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <iomanip>
#include <regex>

using namespace std;

// === Глобальные переменные ===
SQLHENV hEnv = SQL_NULL_HENV;
SQLHDBC hDbc = SQL_NULL_HDBC;
SQLHSTMT hStmt = SQL_NULL_HSTMT;

struct Resource {
    int ResourceID;
    wstring Name;
    long long Size;
    int CategoryID;
    int OwnerID;
    bool isDeleted;
};

// === Утилиты ===
void SetColor(int textColor) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, textColor);
}

wstring StringToWString(const string& s) {
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
    wstring r(buf);
    delete[] buf;
    return r;
}

// === Работа с БД ===
void DisconnectDB() {
    if (hStmt) { SQLFreeHandle(SQL_HANDLE_STMT, hStmt); hStmt = SQL_NULL_HSTMT; }
    if (hDbc) { SQLDisconnect(hDbc); SQLFreeHandle(SQL_HANDLE_DBC, hDbc); hDbc = SQL_NULL_HDBC; }
    if (hEnv) { SQLFreeHandle(SQL_HANDLE_ENV, hEnv); hEnv = SQL_NULL_HENV; }
}

bool ConnectDB(const string& dsnName) {
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);

    wstring wDsnName = StringToWString(dsnName);
    SQLRETURN retCode = SQLConnect(hDbc, (SQLWCHAR*)wDsnName.c_str(), SQL_NTS, NULL, 0, NULL, 0);
    
    if (SQL_SUCCEEDED(retCode)) {
        SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
        return true;
    }
    DisconnectDB();
    return false;
}

// --- Автоматическое логирование ---
void LogAction(const wstring& actionDesc) {
    SQLHSTMT hStmtLog = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmtLog);
    
    wstring query = L"INSERT INTO Logs (ActionDescription) VALUES (?)";
    SQLPrepare(hStmtLog, (SQLWCHAR*)query.c_str(), SQL_NTS);
    
    SQLLEN cbDesc = SQL_NTS;
    SQLBindParameter(hStmtLog, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)actionDesc.c_str(), 0, &cbDesc);
    
    SQLExecute(hStmtLog);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmtLog);
}

// --- Добавление с параметризацией (Защита от инъекций) ---
void AddResource(const wstring& name, long long size, int categoryId, int ownerId) {
    SQLHSTMT hStmtInsert = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmtInsert);

    wstring query = L"INSERT INTO Resources (Name, Size, CategoryID, OwnerID) VALUES (?, ?, ?, ?)";
    SQLPrepare(hStmtInsert, (SQLWCHAR*)query.c_str(), SQL_NTS);

    SQLLEN cbName = SQL_NTS, cbSize = 0, cbCat = 0, cbOwner = 0;
    
    SQLBindParameter(hStmtInsert, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)name.c_str(), 0, &cbName);
    SQLBindParameter(hStmtInsert, 2, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &size, 0, &cbSize);
    SQLBindParameter(hStmtInsert, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &categoryId, 0, &cbCat);
    SQLBindParameter(hStmtInsert, 4, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &ownerId, 0, &cbOwner);

    if (SQL_SUCCEEDED(SQLExecute(hStmtInsert))) {
        SetColor(2);
        wcout << L"Успех: Ресурс добавлен.\n";
        LogAction(L"Добавлен ресурс: " + name);
    } else {
        SetColor(4);
        cout << "Ошибка добавления ресурса в БД.\n";
    }
    SetColor(7);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmtInsert);
}

// --- Soft Delete (Корзина) ---
void ToggleDeleteStatus(int resourceId, bool deleteFlag) {
    SQLHSTMT hStmtToggle = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmtToggle);

    wstring query = L"UPDATE Resources SET isDeleted = ? WHERE ResourceID = ?";
    SQLPrepare(hStmtToggle, (SQLWCHAR*)query.c_str(), SQL_NTS);

    int flagVal = deleteFlag ? 1 : 0;
    SQLLEN cbFlag = 0, cbId = 0;
    
    SQLBindParameter(hStmtToggle, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &flagVal, 0, &cbFlag);
    SQLBindParameter(hStmtToggle, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &resourceId, 0, &cbId);

    if (SQL_SUCCEEDED(SQLExecute(hStmtToggle))) {
        SQLLEN rowCount = 0;
        SQLRowCount(hStmtToggle, &rowCount);
        if (rowCount > 0) {
            SetColor(14); // Желтый
            cout << (deleteFlag ? "Файл перемещен в корзину.\n" : "Файл восстановлен из корзины.\n");
            wstring action = deleteFlag ? L"Удаление (Soft) ID: " : L"Восстановление ID: ";
            LogAction(action + to_wstring(resourceId));
        } else {
            SetColor(4); cout << "Файл с таким ID не найден.\n";
        }
    }
    SetColor(7);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmtToggle);
}

void PingServer() {
    if (hDbc == SQL_NULL_HDBC) return;
    SQLCHAR sqlState[6], msg[256];
    SQLINTEGER nativeError;
    SQLSMALLINT msgLen;
    if (SQLGetDiagRec(SQL_HANDLE_DBC, hDbc, 1, sqlState, &nativeError, msg, sizeof(msg), &msgLen) == SQL_SUCCESS) {
        SetColor(4); cout << "Пинг: Ошибка соединения (" << sqlState << ")\n"; SetColor(7);
    }
}

int main() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);

    cout << "--- ByteKeeper: Система управления активами ---\n";
    if (!ConnectDB("ByteKeeperDSN")) {
        SetColor(4); cout << "Критическая ошибка: Не удалось подключиться к ODBC DSN 'ByteKeeperDSN'.\n"; SetColor(7);
        return 1;
    }

    SetColor(2); cout << "Подключение к БД установлено.\n\n"; SetColor(7);
    
    // Временное меню для тестов (пока без защиты ввода)
    int choice = 0;
    while (choice != 9) {
        cout << "1. Добавить тестовый файл (Параметризация)\n";
        cout << "2. Удалить файл в корзину (Soft Delete)\n";
        cout << "3. Восстановить файл\n";
        cout << "9. Выход\nВыбор: ";
        cin >> choice;

        if (choice == 1) AddResource(L"TestDoc_2026.pdf", 1048576, 1, 1);
        else if (choice == 2) ToggleDeleteStatus(1, true);
        else if (choice == 3) ToggleDeleteStatus(1, false);
    }

    DisconnectDB();
    return 0;
}