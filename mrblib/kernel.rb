module Kernel
  def open(file, *rest, &block)
    raise ArgumentError unless file.is_a?(String)

    if file[0] == "|"
      raise ArgumentError, "IO.popen is not supported on this plathome."
    else
      File.open(file, *rest, &block)
    end
  end
end
