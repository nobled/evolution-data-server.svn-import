#include "camel-test.h"
#include "messages.h"
#include "addresses.h"

/* for stat */
#include <sys/stat.h>
#include <unistd.h>

#include <unicode.h>

#include <camel/camel-internet-address.h>
#include <camel/camel-address.h>

/* a bunch of test strings in different encodings, just taken from gnome-core po files */
/* see data/genline.pl */
struct _l {
    char *type;
    char *line;
} test_lines[] = {
	/* FIXME: for now, remove the types libunicode doesn't know about, this is tricky to fix */
	/* And at best, all we could do is translate it to X-Unknown, or something */
	/*{ "windows-1251", "���� ����� �� �������� �� ������ �� �����.\n�� �� �������� �� ��� �� �������?" },*/
	{ "iso-8859-1", "Omple les miniatures de la finestra amb contingut de la pantalla" },
	{ "ISO-8859-2", "Spr�vce oken h�be s okrajem okna (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
	{ "ISO-8859-1", "Vinduesh�ndtering flytter dekorationsvindue istedet (AfterStep, Enlightenment, FVWM, IceWM, Sawfish)" },
	{ "ISO-8859-1", "Vorschaubilder der Fenster mit dem Bildschirminhalt ausf�llen" },
	{ "iso-8859-7", "�������� �������� ��� �� ��������� ��� ����� ��������� (���������-��������������)" },
	{ "iso-8859-1", "You've chosen to disable the startup hint. To re-enable it, choose \"Startup Hint\" in the GNOME Control Centre" },
	{ "iso-8859-1", "El aplique de reloj muestra en su panel la fecha y la hora de forma simple y ligero " },
	{ "iso-8859-1", "Applet ei vasta salvestusk�sule. Kas peaks ta niisama sulgema, v�i veel ootama?" },
	{ "iso-8859-1", "Lehio kudeatzaileak lehioaren dekorazaioa mugiarazten (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
	{ "iso-8859-15", "N�yt� sovellukset, joiden ikkunoista on n�kyvill� vain otsikkopalkki" },
	{ "ISO-8859-1", "Afficher les t�ches qui ne sont pas dans la liste des fen�tres" },
	{ "iso-8859-1", "N�l applet ag tabhair freagra ar iarratas s�bh�il. Bain amach an applet n� lean ar f�nacht?" },
	{ "iso-8859-1", "Amosa-las tarefas agochadas da lista de fiestras (SKIP-WINLIST)" },
	{ "iso-8859-2", "Az ablakkezel� a dekor�ci�t mozgassa az ablak helyett (AfterStep, Enlightenment, FVWM, IceWM, SawMill)" },
	{ "iso-8859-1", "Riempi la finestra delle anteprime con il contenuto dello schermo" },
	{ "euc-jp", "������ɥ��ޥ͡�����Ͼ��ꥦ����ɥ���ư���� (AfterStep, Enlightenment, FVWM, IceWM, Sawfish)" },
	{ "euc-kr", "â �����ڰ� �ٹ� â ��� �̵� (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
	{ "iso-8859-13", "Priedas neatsakin�ja � pra�ym� i�sisaugoti. Pa�alinti pried� ar laukti toliau?" },
	{ "iso-8859-1", "Window manager verplaatst dekoratie (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
	{ "iso-8859-1", "Vindush�ndtereren flytter dekorasjonsvinduet i stedet (AfterStep, Enlightenment, FVWM, IceWM, Sawfish)" },
	{ "iso-8859-2", "Przemieszczanie dekoracji zamiast okna (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
	{ "iso-8859-1", "Este programa � respons�vel por executar outras aplica��es, embeber pequenos applets, a paz no mundo e crashes aleat�rios do X." },
	{ "iso-8859-1", "Mostrar tarefas que se escondem da lista de janelas (SKIP-WINLIST)" },
	{ "koi8-r", "������ �������� ����� � ������������� ��������� � ������� ������" },
	{ "iso-8859-2", "Spr�vca okien pres�va okraje okien (AfterStep, Enlightenment, FVWM, IceWM, Sawfish)" },
	{ "iso-8859-2", "Ka�i posle, ki se skrivajo pred upravljalnik oken (SKIP-WINLIST)" },
	{ "iso-8859-5", "Window ��������� ������ ����������� ������ ������ ���a (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
	{ "iso-8859-2", "Window menadzeri pomera dekoracioni prozor umesto toga (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
	{ "iso-8859-1", "F�nsterhanteraren flyttar dekorationsf�nstret ist�llet (AfterStep, Enlightenment, FVWM, IceWM, Sawfish)" },
	/*{ "TSCII", "���츼�-���� ���� ��¡� ���츼�� ���� (���츼�-���-�Ţ�)" },*/
	{ "iso-8859-9", "Kaydetme iste�ine bir uygulak cevap vermiyor . Uygula�� sileyim mi , yoksa bekleyeyim mi ?" },
	{ "koi8-u", "����ͦ����� ������æ� ��ͦ��� צ��� (AfterStep, Enlightenment, FVWM, IceWM, Sawfish)" },
	{ "iso-8859-1", "Cwand on scrift�r est bodj� fo�, li scrift�r �t totes les apliketes � dvins sont pierdowes. Bodj� ci scrift�r chal?" },
	{ "gb2312", "Ǩ�Ƶ�װ�δ��ڹ������(AfterStep, Enlightenment, FVWM, IceWM, SawMill)" },
	{ "big5", "�����޲z�̥u���ʸ˹����� (AfterStep, Enlightenment, FVWM, IceWM, Sawmill)" },
};

static char *convert(const char *in, const char *from, const char *to)
{
	unicode_iconv_t ic = unicode_iconv_open(to, from);
	char *out, *outp;
	const char *inp;
	int inlen, outlen;

	if (ic == (unicode_iconv_t)-1)
		return g_strdup(in);

	inlen = strlen(in);
	outlen = inlen*5 + 16;

	outp = out = g_malloc(outlen);
	inp = in;

	if (unicode_iconv(ic, &inp, &inlen, &outp, &outlen) == -1) {
		test_free(out);
		unicode_iconv_close(ic);
		return g_strdup(in);
	}

	if (unicode_iconv(ic, NULL, 0, &outp, &outlen) == -1) {
		test_free(out);
		unicode_iconv_close(ic);
		return g_strdup(in);
	}

	unicode_iconv_close(ic);

	*outp = 0;

#if 0
	/* lets see if we can convert back again? */
	{
		char *nout, *noutp;
		unicode_iconv_t ic = unicode_iconv_open(from, to);

		inp = out;
		inlen = strlen(out);
		outlen = inlen*5 + 16;
		noutp = nout = g_malloc(outlen);
		if (unicode_iconv(ic, &inp, &inlen, &noutp, &outlen) == -1
		    || unicode_iconv(ic, NULL, 0, &noutp, &outlen) == -1) {
			g_warning("Cannot convert '%s' \n from %s to %s: %s\n", in, to, from, strerror(errno));
		}
		unicode_iconv_close(ic);
	}

	/* and lets see what camel thinks out optimal charset is */
	{
		printf("Camel thinks the best encoding of '%s' is %s, although we converted from %s\n",
		       in, camel_charset_best(out, strlen(out)), from);
	}
#endif

	return out;
}

#define to_utf8(in, type) convert(in, type, "utf-8")
#define from_utf8(in, type) convert(in, "utf-8", type)

#define ARRAY_LEN(x) (sizeof(x)/sizeof(x[0]))

int main(int argc, char **argv)
{
	int i;
	CamelInternetAddress *addr, *addr2;
	char *name;
	char *charset;
	const char *real, *where;
	char *enc, *enc2, *format, *format2;

	camel_test_init(argc, argv);

	camel_test_start("CamelInternetAddress, basics");

	addr = camel_internet_address_new();

	push("Test blank address");
	check(camel_address_length(CAMEL_ADDRESS(addr)) == 0);
	check(camel_internet_address_get(addr, 0, &real, &where) == FALSE);
	pull();

	push("Test blank clone");
	addr2 = CAMEL_INTERNET_ADDRESS(camel_address_new_clone(CAMEL_ADDRESS(addr)));
	test_address_compare(addr, addr2);
	check_unref(addr2, 1);
	pull();

	push("Test add 1");
	camel_internet_address_add(addr, "Zed", "nowhere@here.com.au");
	check(camel_address_length(CAMEL_ADDRESS(addr)) == 1);
	check(camel_internet_address_get(addr, 0, &real, &where) == TRUE);
	check_msg(string_equal("Zed", real), "real = '%s'", real);
	check(strcmp(where, "nowhere@here.com.au") == 0);
	pull();

	push("Test clone 1");
	addr2 = CAMEL_INTERNET_ADDRESS(camel_address_new_clone(CAMEL_ADDRESS(addr)));
	test_address_compare(addr, addr2);
	check_unref(addr2, 1);
	pull();

	push("Test add many");
	for (i=1;i<10;i++) {
		char name[16], a[32];
		sprintf(name, "Zed %d", i);
		sprintf(a, "nowhere@here-%d.com.au", i);
		camel_internet_address_add(addr, name, a);
		check(camel_address_length(CAMEL_ADDRESS(addr)) == i+1);
		check(camel_internet_address_get(addr, i, &real, &where) == TRUE);
		check_msg(string_equal(name, real), "name = '%s' real = '%s'", name, real);
		check(strcmp(where, a) == 0);
	}
	pull();

	/* put a few of these in to make it look like its doing something impressive ... :) */
	camel_test_end();
	camel_test_start("CamelInternetAddress, search");

	push("Test search");
	camel_test_nonfatal("Address comparisons should ignore whitespace??");
	check(camel_internet_address_find_name(addr, "Zed 1", &where) == 1);
	check(camel_internet_address_find_name(addr, "Zed 9", &where) == 9);
	check(camel_internet_address_find_name(addr, "Zed", &where) == 0);
	check(camel_internet_address_find_name(addr, " Zed", &where) == 0);
	check(camel_internet_address_find_name(addr, "Zed ", &where) == 0);
	check(camel_internet_address_find_name(addr, "  Zed ", &where) == 0);
	check(camel_internet_address_find_name(addr, "Zed 20", &where) == -1);
	check(camel_internet_address_find_name(addr, "", &where) == -1);
	/* interface dont handle nulls :) */
	/*check(camel_internet_address_find_name(addr, NULL, &where) == -1);*/

	check(camel_internet_address_find_address(addr, "nowhere@here-1.com.au", &where) == 1);
	check(camel_internet_address_find_address(addr, "nowhere@here-1 . com.au", &where) == 1);
	check(camel_internet_address_find_address(addr, "nowhere@here-2 .com.au ", &where) == 2);
	check(camel_internet_address_find_address(addr, " nowhere @here-3.com.au", &where) == 3);
	check(camel_internet_address_find_address(addr, "nowhere@here-20.com.au ", &where) == -1);
	check(camel_internet_address_find_address(addr, "", &where) == -1);
	/*check(camel_internet_address_find_address(addr, NULL, &where) == -1);*/
	camel_test_fatal();
	pull();

	camel_test_end();
	camel_test_start("CamelInternetAddress, copy/cat/clone");

	push("Test clone many");
	addr2 = CAMEL_INTERNET_ADDRESS(camel_address_new_clone(CAMEL_ADDRESS(addr)));
	test_address_compare(addr, addr2);
	pull();

	push("Test remove items");
	camel_address_remove(CAMEL_ADDRESS(addr2), 0);
	check(camel_address_length(CAMEL_ADDRESS(addr2)) == 9);
	camel_address_remove(CAMEL_ADDRESS(addr2), 0);
	check(camel_address_length(CAMEL_ADDRESS(addr2)) == 8);
	camel_address_remove(CAMEL_ADDRESS(addr2), 5);
	check(camel_address_length(CAMEL_ADDRESS(addr2)) == 7);
	camel_address_remove(CAMEL_ADDRESS(addr2), 10);
	check(camel_address_length(CAMEL_ADDRESS(addr2)) == 7);
	camel_address_remove(CAMEL_ADDRESS(addr2), -1);
	check(camel_address_length(CAMEL_ADDRESS(addr2)) == 0);
	check_unref(addr2, 1);
	pull();

	push("Testing copy/cat");
	push("clone + cat");
	addr2 = CAMEL_INTERNET_ADDRESS(camel_address_new_clone(CAMEL_ADDRESS(addr)));
	camel_address_cat(CAMEL_ADDRESS(addr2), CAMEL_ADDRESS(addr));
	check(camel_address_length(CAMEL_ADDRESS(addr)) == 10);
	check(camel_address_length(CAMEL_ADDRESS(addr2)) == 20);
	check_unref(addr2, 1);
	pull();

	push("cat + cat + copy");
	addr2 = camel_internet_address_new();
	camel_address_cat(CAMEL_ADDRESS(addr2), CAMEL_ADDRESS(addr));
	test_address_compare(addr, addr2);
	camel_address_cat(CAMEL_ADDRESS(addr2), CAMEL_ADDRESS(addr));
	check(camel_address_length(CAMEL_ADDRESS(addr)) == 10);
	check(camel_address_length(CAMEL_ADDRESS(addr2)) == 20);
	camel_address_copy(CAMEL_ADDRESS(addr2), CAMEL_ADDRESS(addr));
	test_address_compare(addr, addr2);
	check_unref(addr2, 1);
	pull();

	push("copy");
	addr2 = camel_internet_address_new();
	camel_address_copy(CAMEL_ADDRESS(addr2), CAMEL_ADDRESS(addr));
	test_address_compare(addr, addr2);
	check_unref(addr2, 1);
	pull();

	pull();

	check_unref(addr, 1);

	camel_test_end();

	camel_test_start("CamelInternetAddress, I18N");

	for (i=0;i<ARRAY_LEN(test_lines);i++) {
		push("Testing text line %d (%s) '%s'", i, test_lines[i].type, test_lines[i].line);

		addr = camel_internet_address_new();

		/* first, convert to api format (utf-8) */
		charset = test_lines[i].type;
		name = to_utf8(test_lines[i].line, charset);

		push("Address setup");
		camel_internet_address_add(addr, name, "nobody@nowhere.com");
		check(camel_internet_address_get(addr, 0, &real, &where) == TRUE);
		check_msg(string_equal(name, real), "name = '%s' real = '%s'", name, real);
		check(strcmp(where, "nobody@nowhere.com") == 0);

		check(camel_internet_address_get(addr, 1, &real, &where) == FALSE);
		check(camel_address_length(CAMEL_ADDRESS(addr)) == 1);
		pull();

		push("Address encode/decode");
		enc = camel_address_encode(CAMEL_ADDRESS(addr));

		addr2 = camel_internet_address_new();
		check(camel_address_decode(CAMEL_ADDRESS(addr2), enc) == 1);
		check(camel_address_length(CAMEL_ADDRESS(addr2)) == 1);

		enc2 = camel_address_encode(CAMEL_ADDRESS(addr2));
		check_msg(string_equal(enc, enc2), "enc = '%s' enc2 = '%s'", enc, enc2);
		test_free(enc2);

		push("Compare addresses");
		test_address_compare(addr, addr2);
		pull();
		test_free(enc);
		pull();

		/* FIXME: format/unformat arne't guaranteed to be reversible, at least at the moment */
		camel_test_nonfatal("format/unformat not (yet) reversible for all cases");

		push("Address format/unformat");
		format = camel_address_format(CAMEL_ADDRESS(addr));

		addr2 = camel_internet_address_new();
		check(camel_address_unformat(CAMEL_ADDRESS(addr2), format) == 1);
		check(camel_address_length(CAMEL_ADDRESS(addr2)) == 1);

		format2 = camel_address_format(CAMEL_ADDRESS(addr2));
		check_msg(string_equal(format, format2), "format = '%s\n\tformat2 = '%s'", format, format2);
		test_free(format2);

		/* currently format/unformat doesn't handle ,'s and other special chars at all */
		if (camel_address_length(CAMEL_ADDRESS(addr2)) == 1) {
			push("Compare addresses");
			test_address_compare(addr, addr2);
			pull();
		}

		test_free(format);
		pull();

		camel_test_fatal();

		check_unref(addr2, 1);

		check_unref(addr, 1);
		pull();

	}

	camel_test_end();

	/* FIXME: Add test of decoding of externally defined addresses */
	/* FIXME: Add test of decoding of broken addresses */

	return 0;
}
