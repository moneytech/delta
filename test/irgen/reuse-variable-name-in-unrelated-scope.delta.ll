
define void @_EN4main1fE() {
  %index = alloca i32
  %x = alloca {}
  %1 = alloca {}
  store i32 0, i32* %index
  %2 = call {} @_EN4main1gE()
  store {} %2, {}* %x
  call void @_ENM4main1XI3intE4initE({}* %1)
  call void @_EN4main1XI3intE1gE({}* %1)
  ret void
}

define {} @_EN4main1gE() {
  %x = alloca {}
  call void @_ENM4main1XI3intE4initE({}* %x)
  %x1 = load {}, {}* %x
  ret {} %x1
}

define void @_EN4main1XI3intE1gE({}* %this) {
  %index = alloca i32
  store i32 0, i32* %index
  ret void
}

define void @_ENM4main1XI3intE4initE({}* %this) {
  ret void
}