# -*- coding: us-ascii -*-
# frozen_string_literal: true
=begin
= Info
  'OpenSSL for Ruby 2' project
  Copyright (C) 2002  Michal Rokos <m.rokos@sh.cvut.cz>
  All rights reserved.

= Licence
  This program is licensed under the same licence as Ruby.
  (See the file 'COPYING'.)
=end

require "mkmf"

ssl_dirs = dir_config("openssl")
dir_config_given = ssl_dirs.any?

_, ssl_ldir = ssl_dirs
if ssl_ldir&.split(File::PATH_SEPARATOR)&.none? { |dir| File.directory?(dir) }
  # According to the `mkmf.rb#dir_config`, the `--with-openssl-dir=<dir>` uses
  # the value of the `File.basename(RbConfig::MAKEFILE_CONFIG["libdir"])` as a
  # loaded library directory name.
  ruby_ldir_name = File.basename(RbConfig::MAKEFILE_CONFIG["libdir"])

  raise "OpenSSL library directory could not be found in '#{ssl_ldir}'. " \
    "You might want to fix this error in one of the following ways.\n" \
    "  * Recompile OpenSSL by configuring it with --libdir=#{ruby_ldir_name} " \
    " to specify the OpenSSL library directory.\n" \
    "  * Recompile Ruby by configuring it with --libdir=<dir> to specify the " \
    "Ruby library directory.\n" \
    "  * Compile this openssl gem with --with-openssl-include=<dir> and " \
    "--with-openssl-lib=<dir> options to specify the OpenSSL include and " \
    "library directories."
end

Logging::message "=== OpenSSL for Ruby configurator ===\n"

$defs.push("-D""OPENSSL_SUPPRESS_DEPRECATED")

have_func("rb_io_descriptor", "ruby/io.h")
have_func("rb_io_maybe_wait(0, Qnil, Qnil, Qnil)", "ruby/io.h") # Ruby 3.1
have_func("rb_io_timeout", "ruby/io.h")

Logging::message "=== Checking for system dependent stuff... ===\n"
have_library("nsl", "t_open")
have_library("socket", "socket")
if $mswin || $mingw
  have_library("ws2_32")
end

if $mingw
  append_cflags '-D_FORTIFY_SOURCE=2'
  append_ldflags '-fstack-protector'
  have_library 'ssp'
end

def find_openssl_library
  if $mswin || $mingw
    # required for static OpenSSL libraries
    have_library("crypt32")
  end

  return false unless have_header("openssl/ssl.h")

  ret = have_library("crypto", "CRYPTO_malloc") &&
    have_library("ssl", "SSL_new")
  return ret if ret

  if $mswin
    # OpenSSL >= 1.1.0: libcrypto.lib and libssl.lib.
    if have_library("libcrypto", "CRYPTO_malloc") &&
        have_library("libssl", "SSL_new")
      return true
    end

    # LibreSSL: libcrypto-##.lib and libssl-##.lib, where ## is the ABI version
    # number. We have to find the version number out by scanning libpath.
    libpath = $LIBPATH.dup
    libpath |= ENV["LIB"].split(File::PATH_SEPARATOR)
    libpath.map! { |d| d.tr(File::ALT_SEPARATOR, File::SEPARATOR) }

    ret = [
      ["crypto", "CRYPTO_malloc"],
      ["ssl", "SSL_new"]
    ].all? do |base, func|
      result = false
      libs = ["lib#{base}-[0-9][0-9]", "lib#{base}-[0-9][0-9][0-9]"]
      libs = Dir.glob(libs.map{|l| libpath.map{|d| File.join(d, l + ".*")}}.flatten).map{|path| File.basename(path, ".*")}.uniq
      libs.each do |lib|
        result = have_library(lib, func)
        break if result
      end
      result
    end
    return ret if ret
  end
  return false
end

Logging::message "=== Checking for required stuff... ===\n"
pkg_config_found = !dir_config_given && pkg_config("openssl") && have_header("openssl/ssl.h")

if !pkg_config_found && !find_openssl_library
  Logging::message "=== Checking for required stuff failed. ===\n"
  Logging::message "Makefile wasn't created. Fix the errors above.\n"
  raise "OpenSSL library could not be found. You might want to use " \
    "--with-openssl-dir=<dir> option to specify the prefix where OpenSSL " \
    "is installed."
end

version_ok = if have_macro("LIBRESSL_VERSION_NUMBER", "openssl/opensslv.h")
  is_libressl = true
  checking_for("LibreSSL version >= 3.9.0") {
    try_static_assert("LIBRESSL_VERSION_NUMBER >= 0x30900000L", "openssl/opensslv.h") }
else
  is_openssl = true
  checking_for("OpenSSL version >= 1.1.1") {
    try_static_assert("OPENSSL_VERSION_NUMBER >= 0x10101000L", "openssl/opensslv.h") }
end
unless version_ok
  raise "OpenSSL >= 1.1.1 or LibreSSL >= 3.9.0 is required"
end

# Prevent wincrypt.h from being included, which defines conflicting macro with openssl/x509.h
if is_libressl && ($mswin || $mingw)
  $defs.push("-DNOCRYPT")
end

Logging::message "=== Checking for OpenSSL features... ===\n"
evp_h = "openssl/evp.h".freeze
ts_h = "openssl/ts.h".freeze
ssl_h = "openssl/ssl.h".freeze

# compile options
have_func("RAND_egd()", "openssl/rand.h")

# added in OpenSSL 1.0.2, not in LibreSSL yet
have_func("SSL_CTX_set1_sigalgs_list(NULL, NULL)", ssl_h)
# added in OpenSSL 1.0.2, not in LibreSSL or AWS-LC yet
have_func("SSL_CTX_set1_client_sigalgs_list(NULL, NULL)", ssl_h)

# added in 1.1.0, currently not in LibreSSL
have_func("EVP_PBE_scrypt(\"\", 0, (unsigned char *)\"\", 0, 0, 0, 0, 0, NULL, 0)", evp_h)

# added in OpenSSL 1.1.1 and LibreSSL 3.5.0, then removed in LibreSSL 4.0.0
have_func("EVP_PKEY_check(NULL)", evp_h)

# added in 3.0.0
have_func("SSL_set0_tmp_dh_pkey(NULL, NULL)", ssl_h)
have_func("ERR_get_error_all(NULL, NULL, NULL, NULL, NULL)", "openssl/err.h")
have_func("SSL_CTX_load_verify_file(NULL, \"\")", ssl_h)
have_func("BN_check_prime(NULL, NULL, NULL)", "openssl/bn.h")
have_func("EVP_MD_CTX_get0_md(NULL)", evp_h)
have_func("EVP_MD_CTX_get_pkey_ctx(NULL)", evp_h)
have_func("EVP_PKEY_eq(NULL, NULL)", evp_h)
have_func("EVP_PKEY_dup(NULL)", evp_h)

# added in 3.2.0
have_func("SSL_get0_group_name(NULL)", ssl_h)

# added in 3.4.0
have_func("TS_VERIFY_CTX_set0_certs(NULL, NULL)", ts_h)

# added in 3.5.0
have_func("SSL_get0_peer_signature_name(NULL, NULL)", ssl_h)

Logging::message "=== Checking done. ===\n"

# Append flags from environment variables.
extcflags = ENV["RUBY_OPENSSL_EXTCFLAGS"]
append_cflags(extcflags.split) if extcflags
extldflags = ENV["RUBY_OPENSSL_EXTLDFLAGS"]
append_ldflags(extldflags.split) if extldflags

create_header
create_makefile("openssl")
Logging::message "Done.\n"
