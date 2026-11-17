# מעבדה 2 — UART, פסיקות ו־Modbus

רון לדין 213465974 · דן צ'טירקין 323126854 · NUCLEO-G491RE

## להגשה (טבלה 2 בדף המשימות)

תיקיית `submission/`:

| סעיף | עותק של `main.c` | צילום סקופ |
|---|---|---|
| A1 | `lab2partAtask1.c` | — |
| A2 | `lab2partAtask2.c` | `A2_PC4_frame.jpeg`: מסגרת של לחיצה אחת על B1 ב־PC4 |
| A3 | `lab2partAtask3.c` | `A3_PA5_busy_wait_delay.png`: ההשהיה ב־PA5 |
| B1 | `lab2partBtask1.c` | — |
| B2 | `lab2partBtask2.c` | — |
| B3 | `lab2partBtask3.c` | — |

כל המדידות, ההסברים וערכי ה־WATCH נמצאים ב־`measurements.md`. הצילומים ויומני הבדיקה על הכרטיס נמצאים ב־`captures/<סעיף>/`.

## תוצאות

| סעיף | תוצאה |
|---|---|
| A1 | `Lab 2 ready.` באתחול, ספרה אחת לכל לחיצה. `MODER` של PA2/PA3 = `0xA`, `AFR` = `0xCC` (AF12) |
| A2 | זמן ביט 8.71 µs (צפוי 8.68), קצב 114 754 baud, התו שפוענח `0x37` = `'7'` |
| A3 | A3a: 10/10. A3b: חזר רק `0`, ובשליחה השנייה כלום (`RXNE=0`, `ORE=1`). A3c בפסיקה: 10/10. השהיה של 152 ms למעבר |
| B1 | 20/20 כתיבות ל־LD2 הצליחו בשליחה הראשונה |
| B2 | 40/40 בקשות. חריגות: רגיסטר 9 → קוד 02, פונקציה `0x04` → קוד 01. ברעש 0.3, `bad` = מספר השליחות עם ביט שגוי |
| B3 | CRC בתוכנה 1527 מחזורים (8.98 µs), בחומרה 219 (1.29 µs), פי 6.97 |

## בנייה וצריבה

פרויקט CMake אחד בונה כל סעיף לפי preset:

```powershell
cd "C:\Users\danch\OneDrive\Desktop\Homeworks\lab\lab2"
.\run-task.ps1 A1          # A1, A2, A3a, A3b, A3c, B1, B2, B3: בנייה וצריבה
.\run-task.ps1 B2 -BuildOnly
python lab02_modbus.py     # Part B. קודם לעצור את ה־Serial Monitor
```

בדיקה אוטומטית על הכרטיס (שומרת ב־`captures/`): `python verify_board.py` ו־`python debug_session.py`.

## תיקיות

- `Core/Src/lab2part*.c`: הקוד של כל סעיף (המקור של הקבצים ב־`submission/`)
- `partB/`: עותקי HAL UART ו־MSP ל־Part B, למקרה ש־`lab2.ioc` נוצר בלי LPUART1
- `docs/`: דף המשימות והחוברת של הקורס
