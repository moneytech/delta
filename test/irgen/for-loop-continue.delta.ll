
%StringIterator = type { i8*, i8* }
%StringRef = type { %"ArrayRef<char>" }
%"ArrayRef<char>" = type { i8*, i32 }

@0 = private unnamed_addr constant [4 x i8] c"abc\00"

define i32 @main() {
  %__iterator4 = alloca %StringIterator
  %__str0 = alloca %StringRef
  %ch = alloca i8
  call void @_ENM3std9StringRef4initE7pointerP4char6length4uint(%StringRef* %__str0, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @0, i32 0, i32 0), i32 3)
  %__str01 = load %StringRef, %StringRef* %__str0
  %1 = call %StringIterator @_EN3std9StringRef8iteratorE(%StringRef %__str01)
  store %StringIterator %1, %StringIterator* %__iterator4
  br label %while

while:                                            ; preds = %loop.increment, %0
  %__iterator42 = load %StringIterator, %StringIterator* %__iterator4
  %2 = call i1 @_EN3std14StringIterator8hasValueE(%StringIterator %__iterator42)
  br i1 %2, label %body, label %endwhile

body:                                             ; preds = %while
  %__iterator43 = load %StringIterator, %StringIterator* %__iterator4
  %3 = call i8 @_EN3std14StringIterator5valueE(%StringIterator %__iterator43)
  store i8 %3, i8* %ch
  %ch4 = load i8, i8* %ch
  %4 = icmp eq i8 %ch4, 98
  br i1 %4, label %then, label %else

loop.increment:                                   ; preds = %endif, %then
  call void @_ENM3std14StringIterator9incrementE(%StringIterator* %__iterator4)
  br label %while

endwhile:                                         ; preds = %while
  ret i32 0

then:                                             ; preds = %body
  br label %loop.increment

else:                                             ; preds = %body
  br label %endif

endif:                                            ; preds = %else
  br label %loop.increment
}

declare %StringIterator @_EN3std9StringRef8iteratorE(%StringRef)

declare void @_ENM3std9StringRef4initE7pointerP4char6length4uint(%StringRef*, i8*, i32)

declare i1 @_EN3std14StringIterator8hasValueE(%StringIterator)

declare i8 @_EN3std14StringIterator5valueE(%StringIterator)

declare void @_ENM3std14StringIterator9incrementE(%StringIterator*)
