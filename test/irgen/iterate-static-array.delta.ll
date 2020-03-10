
%"ArrayIterator<int>" = type { i32*, i32* }
%StringBuffer = type { %"List<char>" }
%"List<char>" = type { i8*, i32, i32 }
%string = type { %"ArrayRef<char>" }
%"ArrayRef<char>" = type { i8*, i32 }

@0 = private unnamed_addr constant [6 x i8] c"%.*s\0A\00", align 1
@1 = private unnamed_addr constant [5 x i8] c"%lld\00", align 1

define i32 @main() {
  %__iterator = alloca %"ArrayIterator<int>"
  %1 = alloca [3 x i32]
  %e = alloca i32*
  %a = alloca [2 x i32]
  %__iterator1 = alloca %"ArrayIterator<int>"
  %e6 = alloca i32*
  store [3 x i32] [i32 1, i32 2, i32 3], [3 x i32]* %1
  %2 = getelementptr inbounds [3 x i32], [3 x i32]* %1, i32 0, i32 0
  %3 = getelementptr inbounds i32, i32* %2, i32 3
  %4 = insertvalue %"ArrayIterator<int>" undef, i32* %2, 0
  %5 = insertvalue %"ArrayIterator<int>" %4, i32* %3, 1
  store %"ArrayIterator<int>" %5, %"ArrayIterator<int>"* %__iterator
  br label %loop.condition

loop.condition:                                   ; preds = %loop.increment, %0
  %6 = call i1 @_EN3std13ArrayIteratorI3intE8hasValueE(%"ArrayIterator<int>"* %__iterator)
  br i1 %6, label %loop.body, label %loop.end

loop.body:                                        ; preds = %loop.condition
  %7 = call i32* @_EN3std13ArrayIteratorI3intE5valueE(%"ArrayIterator<int>"* %__iterator)
  store i32* %7, i32** %e
  %e.load = load i32*, i32** %e
  call void @_EN3std5printI3intEE5valueP3int(i32* %e.load)
  br label %loop.increment

loop.increment:                                   ; preds = %loop.body
  call void @_EN3std13ArrayIteratorI3intE9incrementE(%"ArrayIterator<int>"* %__iterator)
  br label %loop.condition

loop.end:                                         ; preds = %loop.condition
  store [2 x i32] [i32 4, i32 5], [2 x i32]* %a
  %8 = getelementptr inbounds [2 x i32], [2 x i32]* %a, i32 0, i32 0
  %9 = getelementptr inbounds i32, i32* %8, i32 2
  %10 = insertvalue %"ArrayIterator<int>" undef, i32* %8, 0
  %11 = insertvalue %"ArrayIterator<int>" %10, i32* %9, 1
  store %"ArrayIterator<int>" %11, %"ArrayIterator<int>"* %__iterator1
  br label %loop.condition2

loop.condition2:                                  ; preds = %loop.increment4, %loop.end
  %12 = call i1 @_EN3std13ArrayIteratorI3intE8hasValueE(%"ArrayIterator<int>"* %__iterator1)
  br i1 %12, label %loop.body3, label %loop.end5

loop.body3:                                       ; preds = %loop.condition2
  %13 = call i32* @_EN3std13ArrayIteratorI3intE5valueE(%"ArrayIterator<int>"* %__iterator1)
  store i32* %13, i32** %e6
  %e6.load = load i32*, i32** %e6
  call void @_EN3std5printI3intEE5valueP3int(i32* %e6.load)
  br label %loop.increment4

loop.increment4:                                  ; preds = %loop.body3
  call void @_EN3std13ArrayIteratorI3intE9incrementE(%"ArrayIterator<int>"* %__iterator1)
  br label %loop.condition2

loop.end5:                                        ; preds = %loop.condition2
  ret i32 0
}

define i1 @_EN3std13ArrayIteratorI3intE8hasValueE(%"ArrayIterator<int>"* %this) {
  %current = getelementptr inbounds %"ArrayIterator<int>", %"ArrayIterator<int>"* %this, i32 0, i32 0
  %current.load = load i32*, i32** %current
  %end = getelementptr inbounds %"ArrayIterator<int>", %"ArrayIterator<int>"* %this, i32 0, i32 1
  %end.load = load i32*, i32** %end
  %1 = icmp ne i32* %current.load, %end.load
  ret i1 %1
}

define i32* @_EN3std13ArrayIteratorI3intE5valueE(%"ArrayIterator<int>"* %this) {
  %current = getelementptr inbounds %"ArrayIterator<int>", %"ArrayIterator<int>"* %this, i32 0, i32 0
  %current.load = load i32*, i32** %current
  ret i32* %current.load
}

define void @_EN3std5printI3intEE5valueP3int(i32* %value) {
  %string = alloca %StringBuffer
  %1 = call %StringBuffer @_EN3std3int8toStringE(i32* %value)
  store %StringBuffer %1, %StringBuffer* %string
  %2 = call i32 @_EN3std12StringBuffer4sizeE(%StringBuffer* %string)
  %3 = call i8* @_EN3std12StringBuffer4dataE(%StringBuffer* %string)
  %4 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([6 x i8], [6 x i8]* @0, i32 0, i32 0), i32 %2, i8* %3)
  call void @_EN3std12StringBuffer6deinitE(%StringBuffer* %string)
  ret void
}

define void @_EN3std13ArrayIteratorI3intE9incrementE(%"ArrayIterator<int>"* %this) {
  %current = getelementptr inbounds %"ArrayIterator<int>", %"ArrayIterator<int>"* %this, i32 0, i32 0
  %current.load = load i32*, i32** %current
  %1 = getelementptr i32, i32* %current.load, i32 1
  store i32* %1, i32** %current
  ret void
}

declare void @_EN3std12StringBuffer6deinitE(%StringBuffer*)

define %StringBuffer @_EN3std3int8toStringE(i32* %this) {
  %result = alloca %StringBuffer
  call void @_EN3std12StringBuffer4initE(%StringBuffer* %result)
  call void @_EN3std3int5printE6streamP12StringBuffer(i32* %this, %StringBuffer* %result)
  %result.load = load %StringBuffer, %StringBuffer* %result
  ret %StringBuffer %result.load
}

declare i32 @printf(i8*, ...)

declare i32 @_EN3std12StringBuffer4sizeE(%StringBuffer*)

declare i8* @_EN3std12StringBuffer4dataE(%StringBuffer*)

declare void @_EN3std12StringBuffer4initE(%StringBuffer*)

define void @_EN3std3int5printE6streamP12StringBuffer(i32* %this, %StringBuffer* %stream) {
  %this.load = load i32, i32* %this
  call void @_EN3std11printSignedI3intEE5value3int6streamP12StringBuffer(i32 %this.load, %StringBuffer* %stream)
  ret void
}

define void @_EN3std11printSignedI3intEE5value3int6streamP12StringBuffer(i32 %value, %StringBuffer* %stream) {
  %result = alloca [22 x i8]
  %1 = alloca %string
  %2 = bitcast [22 x i8]* %result to i8*
  %3 = call i32 (i8*, i8*, ...) @sprintf(i8* %2, i8* getelementptr inbounds ([5 x i8], [5 x i8]* @1, i32 0, i32 0), i32 %value)
  %4 = bitcast [22 x i8]* %result to i8*
  call void @_EN3std6string4initE7cStringP4char(%string* %1, i8* %4)
  %.load = load %string, %string* %1
  %5 = call i1 @_EN3std12StringBuffer5writeE1s6string(%StringBuffer* %stream, %string %.load)
  ret void
}

declare i32 @sprintf(i8*, i8*, ...)

declare i1 @_EN3std12StringBuffer5writeE1s6string(%StringBuffer*, %string)

declare void @_EN3std6string4initE7cStringP4char(%string*, i8*)
