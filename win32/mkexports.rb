#!./miniruby -sI.

$name = $library = $description = nil

module RbConfig
  autoload :CONFIG, "./rbconfig"
end

class Exports
  PrivateNames = /(?:Init_|InitVM_|ruby_static_id_|_threadptr_|_ec_|DllMain\b)/

  def self.create(*args, &block)
    platform = RUBY_PLATFORM
    klass = constants.find do |p|
      break const_get(p) if platform.include?(p.to_s.downcase)
    end
    unless klass
      raise ArgumentError, "unsupported platform: #{platform}"
    end
    klass.new(*args, &block)
  end

  def self.extract(objs, *rest)
    create(objs).exports(*rest)
  end

  def self.output(output = $output, &block)
    if output
      File.open(output, 'wb', &block)
    else
      yield STDOUT
    end
  end

  def initialize(objs)
    syms = {}
    winapis = {}
    syms["ruby_sysinit_real"] = "ruby_sysinit"
    each_export(objs) do |internal, export|
      syms[internal] = export
      winapis[$1] = internal if /^_?(rb_w32_\w+)(?:@\d+)?$/ =~ internal
    end
    incdir = File.join(File.dirname(File.dirname(__FILE__)), "include/ruby")
    read_substitution(incdir+"/win32.h", syms, winapis)
    read_substitution(incdir+"/subst.h", syms, winapis)
    syms["rb_w32_vsnprintf"] ||= "ruby_vsnprintf"
    syms["rb_w32_snprintf"] ||= "ruby_snprintf"
    @syms = syms
  end

  def read_substitution(header, syms, winapis)
    File.foreach(header) do |line|
      if /^#define (\w+)\((.*?)\)\s+(?:\(void\))?(rb_w32_\w+)\((.*?)\)\s*$/ =~ line and
          $2.delete(" ") == $4.delete(" ")
        export, internal = $1, $3
        if syms[internal] or internal = winapis[internal]
          syms[forwarding(internal, export)] = internal
        end
      end
    end
  end

  def exports(name = $name, library = $library, description = $description)
    exports = []
    if name
      exports << "Name " + name
    elsif library
      exports << "Library " + library
    end
    exports << "Description " + description.dump if description
    exports << "VERSION #{RbConfig::CONFIG['MAJOR']}.#{RbConfig::CONFIG['MINOR']}"
    exports << "EXPORTS" << symbols()
    exports
  end

  private
  def forwarding(internal, export)
    internal.sub(/^[^@]+/, "\\1#{export}")
  end

  def each_export(objs)
  end

  def objdump(objs, &block)
    if objs.empty?
      $stdin.each_line(&block)
    else
      each_line(objs, &block)
    end
  end

  def symbols()
    @syms.sort.collect {|k, v| v ? v == true ? "#{k} DATA" : "#{k}=#{v}" : k}
  end
end

class Exports::Mswin < Exports
  def each_line(objs, &block)
#    IO.popen(%w"dumpbin -linkermember:1" + objs) do |f|
    objs.each do |o|
      open(o) do |f|
        f.each(&block)
      end
    end
  end

  def each_export(objs)
    noprefix = ($arch ||= nil and /^(sh|i\d86)/ !~ $arch)
    objs = objs.collect {|s| s.tr('/', '\\')}
    libname = RbConfig::CONFIG["LIBRUBY_A"]
    libname[/\.lib$/]=""
    publics = false
    started = false
    finished = false
    objdump(objs) do |l|
      next if finished
      l.chomp!
      finished ||= started && l == ""
      started ||= publics && l != ""
      publics ||= /^\s+Address\s+Publics by Value\s+Rva\+Base\s+Lib:Object$/.match?(l)

      if started
        /\A\s+\h+:\h+\s+(#{noprefix ? "" : "_"}[a-zA-Z_]\w+#{noprefix ? "" : /(?:@\d+)?/})\s+\h+(?:\s+(f)\s*i?\s+|\s*)(?:#{libname}:[\w-]+\.[\w-]+|<common>)\z/.match(l) do |m|
          sym, f = m[1], m[2]
          if !noprefix && /^[@_]/.match?(sym) && !/@\d+$/.match?(sym)
            sym.sub!(/^[@_]/, '')
          end
          next if /(?:_local_stdio_printf_options|\Av(f|sn?)?printf(_s)?(_l)?)\Z/.match?(sym)
          next if /\A_[^@]*\z|^#{PrivateNames}/o.match?(sym)
          next if /@[[:xdigit:]]{8,32}\z/.match?(sym)
          yield sym, f != "f"
        end
      end
      next

      symbols ||= /^\s*\d+\spublic symbols$/.match? l
      symbols &&= !(/^Archive member name/.match? l)
      if symbols
        l.chomp!
        next if l == ""

        /^\s*\h+\s(#{noprefix ? "" : "_"}[a-zA-Z_]\w+)$/.match(l) do |m|
          is_data = ""
          next if /^_?#{PrivateNames}/o.match?(m[1])
          yield m[1], nil
        end
        next

        case symbols
        when /OBJECT/, /LIBRARY/
          next if (/^ .*\(pick any\)$/ =~ l)...true
          next if /^[[:xdigit:]]+ 0+ UNDEF / =~ l
          next unless /External/ =~ l
          next if /(?:_local_stdio_printf_options|v(f|sn?)printf(_s)?_l)\Z/ =~ l
          next unless l.sub!(/.*?\s(\(\)\s+)?External\s+\|\s+/, '')
          is_data = !$1
          if noprefix or /^[@_]/ =~ l
            next if /(?!^)@.*@/ =~ l || /@[[:xdigit:]]{8,32}$/ =~ l ||
                    /^_?#{PrivateNames}/o =~ l
            l.sub!(/^[@_]/, '') if /@\d+$/ !~ l
          elsif !l.sub!(/^(\S+) \([^@?\`\']*\)$/, '\1')
            next
          end
        when /DLL/
          next unless l.sub!(/^\s*\d+\s+[[:xdigit:]]+\s+[[:xdigit:]]+\s+/, '')
        else
          next
        end
        yield l.strip, is_data
      end
    end
    yield "strcasecmp", "msvcrt.stricmp"
    yield "strncasecmp", "msvcrt.strnicmp"
  end
end

class Exports::Cygwin < Exports
  def self.nm
    @@nm ||=
      begin
        require 'shellwords'
        RbConfig::CONFIG["NM"].shellsplit
      end
  end

  def exports(*)
    super()
  end

  def each_line(objs, &block)
    IO.popen([*self.class.nm, *%w[--extern-only --defined-only], *objs]) do |f|
      f.each(&block)
    end
  end

  def each_export(objs)
    symprefix = RbConfig::CONFIG["SYMBOL_PREFIX"]
    symprefix.strip! if symprefix
    re = /\s(?:(T)|[[:upper:]])\s#{symprefix}((?!#{PrivateNames}).*)$/
    objdump(objs) do |l|
      next if /@.*@/ =~ l
      yield $2.strip, !$1 if re =~ l
    end
  end
end

class Exports::Msys < Exports::Cygwin
end

class Exports::Mingw < Exports::Cygwin
  def each_export(objs)
    super
    yield "strcasecmp", "_stricmp"
    yield "strncasecmp", "_strnicmp"
  end
end

END {
  exports = Exports.extract(ARGV)
  Exports.output {|f| f.puts(*exports)}
}
