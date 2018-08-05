# includes append to these:
SUFFIXES =
TESTS =
CLEANFILES =
DISTCLEANFILES =
bin_PROGRAMS =
noinst_HEADERS =
lib_LTLIBRARIES =
man_MANS =
noinst_LTLIBRARIES =
noinst_PROGRAMS =
include_HEADERS =
nobase_include_HEADERS =
check_PROGRAMS =
EXTRA_HEADERS =
BUILT_SOURCES=
EXTRA_DIST=
dist_doc_DATA=
dist_noinst_SCRIPTS =
noinst_SCRIPTS =
check_SCRIPTS =

#includes additional rules from aminclude.am
@INC_AMINCLUDE@
DISTCLEANFILES+= aminclude.am

CLEANFILES+= cert.der \
             cert.pem \
             certecc.der \
             certecc.pem \
             certreq.der \
             certreq.pem \
             key.der \
             key.pem \
             ntru-cert.der \
             ecc-key-pkcs8.der \
             ntru-cert.pem \
             ntru-key.raw \
             othercert.der \
             othercert.pem \
             pkcs7cert.der \
             pkcs7encryptedDataAES128CBC.der \
             pkcs7encryptedDataAES192CBC.der \
             pkcs7encryptedDataAES256CBC_attribs.der \
             pkcs7encryptedDataAES256CBC.der \
             pkcs7encryptedDataAES256CBC_multi_attribs.der \
             pkcs7encryptedDataDES3.der \
             pkcs7encryptedDataDES.der \
             pkcs7envelopedDataAES256CBC_ECDH.der \
             pkcs7envelopedDataAES128CBC_ECDH_SHA1KDF.der \
             pkcs7envelopedDataAES256CBC_ECDH_SHA256KDF.der \
             pkcs7envelopedDataAES256CBC_ECDH_SHA512KDF.der \
             pkcs7envelopedDataAES256CBC_ECDH_SHA512KDF_ukm.der \
             pkcs7envelopedDataDES3.der \
             pkcs7envelopedDataAES128CBC.der \
             pkcs7envelopedDataAES192CBC.der \
             pkcs7envelopedDataAES256CBC.der \
             pkcs7signedData_RSA_SHA.der \
             pkcs7signedData_RSA_SHA_noattr.der \
             pkcs7signedData_RSA_SHA224.der \
             pkcs7signedData_RSA_SHA256.der \
             pkcs7signedData_RSA_SHA384.der \
             pkcs7signedData_RSA_SHA512.der \
             pkcs7signedData_ECDSA_SHA.der \
             pkcs7signedData_ECDSA_SHA_noattr.der \
             pkcs7signedData_ECDSA_SHA224.der \
             pkcs7signedData_ECDSA_SHA256.der \
             pkcs7signedData_ECDSA_SHA384.der \
             pkcs7signedData_ECDSA_SHA512.der

exampledir = $(docdir)/example
dist_example_DATA=

ACLOCAL_AMFLAGS= -I tools/m4

EXTRA_DIST+= others/lib/dummy

EXTRA_DIST+= wolfssl-ntru.vcproj
EXTRA_DIST+= wolfssl.vcproj
EXTRA_DIST+= wolfssl.vcxproj
EXTRA_DIST+= wolfssl-ntru.sln
EXTRA_DIST+= wolfssl.sln
EXTRA_DIST+= wolfssl64.sln
EXTRA_DIST+= valgrind-error.sh
EXTRA_DIST+= gencertbuf.pl
EXTRA_DIST+= README.md
EXTRA_DIST+= README
EXTRA_DIST+= ChangeLog.md
EXTRA_DIST+= LICENSING
EXTRA_DIST+= INSTALL
EXTRA_DIST+= IPP
EXTRA_DIST+= .cproject
EXTRA_DIST+= .project

include wrapper/include.am
include cyassl/include.am
include wolfssl/include.am
include certs/include.am
include doc/include.am
include wrapper/swig/include.am

include src/include.am
include others/support/include.am
include wolfcrypt/user-crypto/include.am
include wolfcrypt/benchmark/include.am
include wolfcrypt/src/include.am
include wolfcrypt/test/include.am
include examples/include.am
include tests/testsuite/include.am
include tests/include.am
include tools/sslSniffer/sslSnifferTest/include.am
include packages/rpm/include.am

# Exclude references to non-DFSG sources from build files
if !BUILD_DISTRO
include IDE/mqx/util_lib/Sources/include.am
include IDE/mqx/wolfcrypt_benchmark/Sources/include.am
include IDE/mqx/wolfcrypt_test/Sources/include.am
include IDE/mqx/wolfssl/include.am
include IDE/mqx/wolfssl_client/Sources/include.am
include IDE/Microchip/mplabx/include.am
include IDE/Microchip/mplabx/wolfcrypt_benchmark.X/nbproject/include.am
include IDE/Microchip/mplabx/wolfcrypt_test.X/nbproject/include.am
include IDE/Microchip/mplabx/wolfssl.X/nbproject/include.am
include IDE/Microchip/mcapi/include.am
include IDE/Microchip/mcapi/wolfcrypt_mcapi.X/nbproject/include.am
include IDE/Microchip/mcapi/wolfcrypt_test.X/nbproject/include.am
include IDE/Microchip/mcapi/wolfssl.X/nbproject/include.am
include IDE/Microchip/mcapi/zlib.X/nbproject/include.am
include IDE/tirtos/include.am
include IDE/include.am
endif
include tests/scripts/include.am

if USE_VALGRIND
TESTS_ENVIRONMENT=.tools/valgrind-error.sh
endif

TEST_EXTENSIONS=.test
TESTS += $(check_PROGRAMS)

check_SCRIPTS+= $(dist_noinst_SCRIPTS)
TESTS += $(check_SCRIPTS)

test: check
tests/unit.log: tests/testsuite/testsuite.log

DISTCLEANFILES+= cyassl-config
DISTCLEANFILES+= wolfssl-config

maintainer-clean-local:
	-rm Makefile.in
	-rm aclocal.m4
	-rm build-aux/compile
	-rm build-aux/config.guess
	-rm build-aux/config.sub
	-rm build-aux/depcomp
	-rm build-aux/install-sh
	-rm build-aux/ltmain.sh
	-rm build-aux/missing
	-rm cyassl-config
	-rmdir build-aux
	-rm configure
	-rm config.log
	-rm config.status
	-rm config.in
	-rm tools/m4/libtool.m4
	-rm tools/m4/ltoptions.m4
	-rm tools/m4/ltsugar.m4
	-rm tools/m4/ltversion.m4
	-rm tools/m4/lt~obsolete.m4
	-rm others/support/wolfssl.pc
	find . -type f -name '*~' -exec rm -f '{}' \;
	-rm -f @PACKAGE@-*.tar.gz
	-rm -f @PACKAGE@-*.rpm

# !!!! first line of rule has to start with a hard (real) tab, not spaces
egs:
	$(MAKE) examples/client/client; \
	$(MAKE) examples/echoclient/echoclient;\
	$(MAKE) examples/server/server; \
	$(MAKE) examples/echoserver/echoserver;

wc_egs:
	$(MAKE) wolfcrypt/test/testwolfcrypt; \
	$(MAKE) wolfcrypt/benchmark/benchmark;

install-exec-local:	install-generic-config

install-generic-config:
	$(mkinstalldirs) $(DESTDIR)$(bindir)
	$(INSTALL_SCRIPT) @GENERIC_CONFIG@ $(DESTDIR)$(bindir)

uninstall-local:
	-rm -f $(DESTDIR)$(bindir)/@GENERIC_CONFIG@

merge-clean:
	@find ./ | $(GREP) \.gcda | xargs rm -f
	@find ./ | $(GREP) \.gcno | xargs rm -f
	@find ./ | $(GREP) \.gz | xargs rm -f
	@find ./ | $(GREP) \.orig | xargs rm -f
	@find ./ | $(GREP) \.rej | xargs rm -f
	@find ./ | $(GREP) \.rpm | xargs rm -f
	@find ./ | $(GREP) \.THIS | xargs rm -f
	@find ./ | $(GREP) \.OTHER | xargs rm -f
	@find ./ | $(GREP) \.BASE | xargs rm -f
	@find ./ | $(GREP) \~$$ | xargs rm -f

