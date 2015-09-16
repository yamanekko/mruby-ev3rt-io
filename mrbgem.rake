MRuby::Gem::Specification.new('mruby-ev3rt-io') do |spec|
  spec.license = 'MIT'
  spec.authors = 'Team Yamanekko'
  spec.summary = 'IO class for EV3RT'

  spec.cc.include_paths << "#{build.root}/src"
end
