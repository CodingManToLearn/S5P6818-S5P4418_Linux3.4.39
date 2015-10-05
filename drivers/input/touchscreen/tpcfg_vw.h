#define	  O_L 1
#define	  O_R 2
#define	  O_U 3
#define	  O_D 4

#define	  INT_U 1
#define	  INT_D 2

struct _position_{
	unsigned short x;
	unsigned short y;
};


struct _TP_CONFIG{
	unsigned char TPID;
	unsigned char origin;
	unsigned char keynum;
	unsigned char inttype;
	unsigned short tpmax_x;
	unsigned short tpmax_y;
	unsigned short tpdplay_x;
	unsigned short tpdplay_y;
	unsigned short keyid[8];
	struct _position_ key_position[8];

};

//POWER��KEY_FN_F11
//NAVI��KEY_FN_F12

#define CFGNUM 23
struct _TP_CONFIG tpconfig[CFGNUM]={
/*ZP-2008*/{0x41,O_R,5,INT_D,1082,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1078,62},{1078,137},{1078,213},{1078,287},{1078,362},{0,0},{0,0},{0,0}}},
/*ZP-2007*/{0x42,O_R,5,INT_D,1082,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1078,465},{1078,495},{1078,525},{1078,557},{1078,596},{0,0},{0,0},{0,0}}},
/*ZP-2009*/{0x43,O_R,5,INT_D,1083,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1079,35},{1079,87},{1079,162},{1079,212},{1079,287},{0,0},{0,0},{0,0}}},
/*ZP-2002*/{0x44,O_R,5,INT_D,1078,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1074,35},{1074,87},{1074,137},{1074,187},{1074,262},{0,0},{0,0},{0,0}}},
/*ZP-2071*/{0x45,O_R,5,INT_D,1082,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1078,442},{1078,480},{1078,517},{1078,555},{1078,596},{0,0},{0,0},{0,0}}},
/*ZP-2036*/{0x47,O_R,5,INT_D,1083,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1079,87},{1079,187},{1079,287},{1079,387},{1079,487},{0,0},{0,0},{0,0}}},
/*ZP-2053*/{0x48,O_R,5,INT_D,1080,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1075,87},{1075,137},{1075,187},{1075,237},{1075,287},{0,0},{0,0},{0,0}}},
/*ZP-2052*/{0x49,O_R,5,INT_D,1080,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1076,466},{1076,497},{1076,526},{1076,556},{1076,594},{0,0},{0,0},{0,0}}},
/*ZP-2005*/{0x4A,O_R,5,INT_D,1080,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1076,62},{1076,162},{1076,262},{1076,362},{1076,462},{0,0},{0,0},{0,0}}},
/*ZP-2001*/{0x4B,O_R,5,INT_D,1082,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1078,35},{1078,87},{1078,162},{1078,212},{1078,287},{0,0},{0,0},{0,0}}},
/*ZP-2010*/{0x4C,O_R,5,INT_D,1083,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1079,35},{1079,87},{1079,162},{1079,212},{1079,287},{0,0},{0,0},{0,0}}},
/*ZP-2006*/{0x4D,O_R,6,INT_D,1078,600,1024,600,{KEY_BACK,KEY_HOMEPAGE,KEY_HOME,KEY_VOLUMEUP,KEY_POWER,KEY_FN_F12,0,0},{{1074,165},{1074,135},{1074,105},{1074,74},{1074,42},{1074,3},{0,0},{0,0}}},
/*ZP-2003*/{0x4E,O_R,6,INT_D,1080,600,1024,600,{KEY_HOMEPAGE,KEY_BACK,KEY_FN_F12,KEY_FN_F10,KEY_VIDEO_NEXT,KEY_VIDEO_PREV,0,0},{{1076,87},{1076,137},{1076,187},{1076,2},{1076,35},{1076,60},{0,0},{0,0}}},
/*ZP-9295*/{0x4F,O_R,5,INT_U,1083,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,0,0,0},{{1079,62},{1079,137},{1079,212},{1079,287},{1079,362},{0,0},{0,0},{0,0}}},
/*ZP-2026*/{0x50,O_R,5,INT_D,1084,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1074,50},{1074,162},{1074,262},{1074,363},{1074,465},{0,0},{0,0},{0,0}}},
/*ZP-2024*/{0x51,O_R,5,INT_D,1083,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1079,83},{1079,183},{1079,316},{1079,416},{1079,552},{0,0},{0,0},{0,0}}},
/*ZP-2062*/{0x52,O_R,5,INT_D,1082,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1078,62},{1078,162},{1078,262},{1078,362},{1078,487},{0,0},{0,0},{0,0}}},
/*ZP-2057*/{0x53,O_R,5,INT_D,1073,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1069,3},{1069,116},{1069,216},{1069,350},{1069,450},{0,0},{0,0},{0,0}}},
/*ZP-2091*/{0x54,O_R,5,INT_D,1073,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1069,158},{1069,121},{1069,85},{1069,50},{1069,3},{0,0},{0,0},{0,0}}},
/*ZP-2013*/{0x55,O_R,5,INT_D,1073,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1069,450},{1069,485},{1069,519},{1069,555},{1069,596},{0,0},{0,0},{0,0}}},
/*ZP-2012*/{0x56,O_R,5,INT_D,1080,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1075,83},{1075,150},{1075,250},{1075,316},{1075,383},{0,0},{0,0},{0,0}}},
/*ZP-2087*/{0x57,O_R,5,INT_D,1073,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1069,450},{1069,483},{1069,516},{1069,553},{1069,596},{0,0},{0,0},{0,0}}},
/*ZP-2015*/{0x58,O_R,5,INT_D,1074,600,1024,600,{KEY_FN_F11,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEUP,KEY_VOLUMEDOWN,KEY_FN_F12,0,0},{{1070,450},{1070,555},{1070,595},{1070,485},{1070,520},{0,0},{0,0},{0,0}}},
};

u8 cfgslt = 0;
struct _position_ *pkey_position=NULL;
unsigned short *pkeyid=NULL;
