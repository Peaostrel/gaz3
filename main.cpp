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

// === Утилиты ===
void SetColor(int textColor) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, textColor);
}

wstring StringToWString(const string& s) {
    int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, NULL, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, buf, len);
    wstring r(buf);
    delete[] buf;
    return r;
}

// Обрезка по ТЗ (до 15 символов + ...)
wstring Truncate(const wstring& str) {
    if (str.length() > 15) return str.substr(0, 15) + L"...";
    return str;
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
    wstring wDsn = StringToWString(dsnName);
    if (SQL_SUCCEEDED(SQLConnect(hDbc, (SQLWCHAR*)wDsn.c_str(), SQL_NTS, NULL, 0, NULL, 0))) {
        SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
        return true;
    }
    DisconnectDB();
    return false;
}

void LogAction(const wstring& actionDesc) {
    SQLHSTMT hLog;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hLog);
    wstring query = L"INSERT INTO Logs (ActionDescription) VALUES (?)";
    SQLPrepare(hLog, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cb = SQL_NTS;
    SQLBindParameter(hLog, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)actionDesc.c_str(), 0, &cb);
    SQLExecute(hLog);
    SQLFreeHandle(SQL_HANDLE_STMT, hLog);
}

// === Валидация (Группа Б) ===
bool IsValidName(const wstring& name) {
    wregex pattern(L"[\\\\/:*?\"<>|]");
    return !regex_search(name, pattern);
}

bool IsAllowedExtension(const wstring& name) {
    size_t pos = name.find_last_of(L".");
    if (pos == wstring::npos && name.find(L"FOLDER") != wstring::npos) return true; // Разрешаем папки
    if (pos == wstring::npos) return false;
    wstring ext = name.substr(pos);
    return (ext == L".exe" || ext == L".txt" || ext == L".pdf");
}

bool IsDuplicate(const wstring& name) {
    SQLHSTMT hDup;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hDup);
    wstring query = L"SELECT COUNT(*) FROM Resources WHERE Name = ?";
    SQLPrepare(hDup, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cb = SQL_NTS;
    SQLBindParameter(hDup, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)name.c_str(), 0, &cb);
    SQLExecute(hDup);
    SQLINTEGER count = 0; SQLLEN cbCount = 0;
    SQLBindCol(hDup, 1, SQL_C_SLONG, &count, 0, &cbCount);
    SQLFetch(hDup);
    SQLFreeHandle(SQL_HANDLE_STMT, hDup);
    return count > 0;
}

// === CRUD и Статистика ===
void AddResource(const wstring& name, long long size, int catId, int ownerId) {
    if (!IsValidName(name)) { SetColor(4); wcout << L"Ошибка: Имя содержит запрещенные символы.\n"; SetColor(7); return; }
    if (!IsAllowedExtension(name)) { SetColor(4); wcout << L"Ошибка: Расширение не в белом списке (.exe, .txt, .pdf).\n"; SetColor(7); return; }
    if (IsDuplicate(name)) { SetColor(4); wcout << L"Ошибка: Файл с таким именем уже существует (Дубликат).\n"; SetColor(7); return; }

    SQLHSTMT hIns;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hIns);
    wstring query = L"INSERT INTO Resources (Name, Size, CategoryID, OwnerID) VALUES (?, ?, ?, ?)";
    SQLPrepare(hIns, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLLEN cb1 = SQL_NTS, cb2 = 0, cb3 = 0, cb4 = 0;
    SQLBindParameter(hIns, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 255, 0, (SQLPOINTER)name.c_str(), 0, &cb1);
    SQLBindParameter(hIns, 2, SQL_PARAM_INPUT, SQL_C_SBIGINT, SQL_BIGINT, 0, 0, &size, 0, &cb2);
    SQLBindParameter(hIns, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &catId, 0, &cb3);
    SQLBindParameter(hIns, 4, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &ownerId, 0, &cb4);

    if (SQL_SUCCEEDED(SQLExecute(hIns))) {
        SetColor(2); wcout << L"Успех: " << name << L" загружен.\n"; SetColor(7);
        LogAction(L"Добавлен: " + name);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hIns);
}

void ShowStatistics() {
    SQLHSTMT hStat;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStat);
    wstring query = L"SELECT COUNT(*), ISNULL(SUM(Size), 0) FROM Resources WHERE isDeleted = 0";
    SQLExecuteDirect(hStat, (SQLWCHAR*)query.c_str(), SQL_NTS);
    SQLINTEGER count = 0; SQLBIGINT totalSize = 0; SQLLEN cb1 = 0, cb2 = 0;
    SQLBindCol(hStat, 1, SQL_C_SLONG, &count, 0, &cb1);
    SQLBindCol(hStat, 2, SQL_C_SBIGINT, &totalSize, 0, &cb2);
    
    if (SQLFetch(hStat) == SQL_SUCCESS) {
        SetColor(11);
        cout << "\n--- СТАТИСТИКА БАЗЫ ---\n";
        cout << "Всего активных файлов: " << count << "\n";
        cout << "Общий объем (байт): " << totalSize << "\n-----------------------\n";
        SetColor(7);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hStat);
}

// Динамическая ширина + Постраничный вывод
void ShowResourcesPaged(int page, int limit) {
    SQLHSTMT hPage;
    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hPage);
    
    // Сначала узнаем макс длину для setw (Требование В)
    wstring qMax = L"SELECT ISNULL(MAX(LEN(Name)), 10) FROM Resources WHERE isDeleted = 0";
    SQLExecuteDirect(hPage, (SQLWCHAR*)qMax.c_str(), SQL_NTS);
    SQLINTEGER maxLen = 10; SQLLEN cbMax = 0;
    SQLBindCol(hPage, 1, SQL_C_SLONG, &maxLen, 0, &cbMax);
    SQLFetch(hPage);
    SQLFreeHandle(SQL_HANDLE_STMT, hPage);
    if (maxLen > 18) maxLen = 18; // С учетом обрезки до 15 + "..."

    SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hPage);
    wstring query = L"SELECT ResourceID, Name, Size FROM Resources WHERE isDeleted = 0 ORDER BY ResourceID OFFSET ? ROWS FETCH NEXT ? ROWS ONLY";
    SQLPrepare(hPage, (SQLWCHAR*)query.c_str(), SQL_NTS);
    
    int offset = (page - 1) * limit;
    SQLLEN cbOff = 0, cbLim = 0;
    SQLBindParameter(hPage, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &offset, 0, &cbOff);
    SQLBindParameter(hPage, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &limit, 0, &cbLim);
    
    SQLExecute(hPage);
    
    SQLINTEGER id; SQLWCHAR name[256]; SQLBIGINT size; SQLLEN cbId = 0, cbName = 0, cbSize = 0;
    SQLBindCol(hPage, 1, SQL_C_SLONG, &id, 0, &cbId);
    SQLBindCol(hPage, 2, SQL_C_WCHAR, name, sizeof(name), &cbName);
    SQLBindCol(hPage, 3, SQL_C_SBIGINT, &size, 0, &cbSize);

    cout << "\nСтраница " << page << " (Лимит: " << limit << ")\n";
    cout << left << setw(5) << "ID" << setw(maxLen + 2) << "Имя" << "Размер\n";
    cout << string(30, '-') << "\n";

    bool hasData = false;
    while (SQLFetch(hPage) == SQL_SUCCESS) {
        hasData = true;
        wstring wName(name);
        wName = Truncate(wName);
        
        cout << left << setw(5) << id;
        
        if (wName.find(L"FOLDER") != wstring::npos) SetColor(14); // Желтый для папок
        wcout << setw(maxLen + 2) << wName;
        SetColor(7);
        
        cout << size << " B\n";
    }
    if (!hasData) cout << "Пусто.\n";
    cout << string(30, '-') << "\n";
    SQLFreeHandle(SQL_HANDLE_STMT, hPage);
}

int main() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);

    if (!ConnectDB("ByteKeeperDSN")) {
        SetColor(4); cout << "Критическая ошибка: Нет подключения к БД.\n"; SetColor(7);
        return 1;
    }

    int choice = 0;
    int currentPage = 1;
    while (choice != 9) {
        cout << "\n1. Добавить файл (С проверками)\n";
        cout << "2. Показать статистику (COUNT/SUM)\n";
        cout << "3. Страница вперед (Пагинация)\n";
        cout << "4. Страница назад\n";
        cout << "9. Выход\nВыбор: ";
        if (!(cin >> choice)) {
            cin.clear(); cin.ignore(10000, '\n');
            SetColor(4); cout << "Ошибка: Введите число!\n"; SetColor(7);
            continue;
        }

        if (choice == 1) {
            AddResource(L"report_fin.pdf", 5000, 1, 1);
            AddResource(L"virus.sh", 120, 1, 1); // Не пройдет
            AddResource(L"MY_FOLDER", 0, 1, 1); // Папка (желтая)
        }
        else if (choice == 2) ShowStatistics();
        else if (choice == 3) { currentPage++; ShowResourcesPaged(currentPage, 3); }
        else if (choice == 4) { if(currentPage > 1) currentPage--; ShowResourcesPaged(currentPage, 3); }
        else if (choice == 9) break;
    }

    DisconnectDB();
    return 0;
}