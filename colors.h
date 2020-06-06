#ifndef _COLORS_
#define _COLORS_

/* FOREGROUND */
#define RST  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KLGRAY  "\x1B[37m"
#define KLRED  "\x1B[91m"
#define KLGREEN "\x1B[92m"
#define KLYELLOW  "\x1B[93m"
#define KLBLUE  "\x1B[94m"
#define KLMAG  "\x1B[95m"

#define RED(x) KRED x RST
#define GREEN(x) KGRN x RST
#define YELL(x) KYEL x RST
#define BLUE(x) KBLU x RST
#define MAG(x) KMAG x RST
#define CYN(x) KCYN x RST
#define GRAY(x) KLGRAY x RST
#define LRED(x) KLRED x RST
#define LGREEN(x) KLGREEN x RST
#define LYELLOW(x) KLYELLOW x RST
#define LBLUE(x) KLBLUE x RST
#define LMAG(x) KLMAG x RST



#define BOLD(x) "\x1B[1m" x RST
#define UNDL(x) "\x1B[4m" x RST

#endif  /* _COLORS_ */