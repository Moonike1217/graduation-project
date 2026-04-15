; ModuleID = '/src/my_pass/build/test.ll'
source_filename = "/src/my_pass/test.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "aarch64-unknown-linux-gnu"

%struct.DataPacket = type { i32, [32 x i8] }

@.str = private unnamed_addr constant [5 x i8] c"READ\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [6 x i8] c"WRITE\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [4 x i8] c"UNK\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [68 x i8] c"[Runtime Report] ID: 0x%08x | Line: %-3d | Mode: %-5s | Index: %ld\0A\00", align 1, !dbg !17
@global_scores = dso_local global [5 x i32] [i32 10, i32 20, i32 30, i32 40, i32 50], align 4, !dbg !22
@.str.4 = private unnamed_addr constant [16 x i8] c"Matrix val: %c\0A\00", align 1, !dbg !27
@.str.5 = private unnamed_addr constant [20 x i8] c"Packet payload: %c\0A\00", align 1, !dbg !32
@.str.6 = private unnamed_addr constant [47 x i8] c"--- \E5\BC\80\E5\A7\8B\E5\85\A8\E9\87\8F\E8\AF\86\E5\88\AB\E6\B5\8B\E8\AF\95 (-O0 \E6\A8\A1\E5\BC\8F) ---\0A\00", align 1, !dbg !37
@.str.7 = private unnamed_addr constant [22 x i8] c"--- \E6\B5\8B\E8\AF\95\E7\BB\93\E6\9D\9F ---\0A\00", align 1, !dbg !42

; Function Attrs: noinline nounwind uwtable
define dso_local void @__afl_report_array(i32 noundef %id, i64 noundef %index, i32 noundef %type, i32 noundef %line) #0 !dbg !57 {
entry:
  %id.addr = alloca i32, align 4
  %index.addr = alloca i64, align 8
  %type.addr = alloca i32, align 4
  %line.addr = alloca i32, align 4
  %mode = alloca ptr, align 8
  store i32 %id, ptr %id.addr, align 4
    #dbg_declare(ptr %id.addr, !72, !DIExpression(), !73)
  store i64 %index, ptr %index.addr, align 8
    #dbg_declare(ptr %index.addr, !74, !DIExpression(), !75)
  store i32 %type, ptr %type.addr, align 4
    #dbg_declare(ptr %type.addr, !76, !DIExpression(), !77)
  store i32 %line, ptr %line.addr, align 4
    #dbg_declare(ptr %line.addr, !78, !DIExpression(), !79)
    #dbg_declare(ptr %mode, !80, !DIExpression(), !83)
  %0 = load i32, ptr %type.addr, align 4, !dbg !84
  %cmp = icmp eq i32 %0, 1, !dbg !85
  br i1 %cmp, label %cond.true, label %cond.false, !dbg !86

cond.true:                                        ; preds = %entry
  br label %cond.end, !dbg !86

cond.false:                                       ; preds = %entry
  %1 = load i32, ptr %type.addr, align 4, !dbg !87
  %cmp1 = icmp eq i32 %1, 2, !dbg !88
  %2 = zext i1 %cmp1 to i64, !dbg !87
  %cond = select i1 %cmp1, ptr @.str.1, ptr @.str.2, !dbg !87
  br label %cond.end, !dbg !86

cond.end:                                         ; preds = %cond.false, %cond.true
  %cond2 = phi ptr [ @.str, %cond.true ], [ %cond, %cond.false ], !dbg !86
  store ptr %cond2, ptr %mode, align 8, !dbg !83
  %3 = load i32, ptr %id.addr, align 4, !dbg !89
  %4 = load i32, ptr %line.addr, align 4, !dbg !90
  %5 = load ptr, ptr %mode, align 8, !dbg !91
  %6 = load i64, ptr %index.addr, align 8, !dbg !92
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str.3, i32 noundef %3, i32 noundef %4, ptr noundef %5, i64 noundef %6), !dbg !93
  ret void, !dbg !94
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind uwtable
define dso_local void @test_array_operations(i32 noundef %idx) #0 !dbg !95 {
entry:
  %idx.addr = alloca i32, align 4
  %local_data = alloca [10 x float], align 4
  store i32 %idx, ptr %idx.addr, align 4
    #dbg_declare(ptr %idx.addr, !98, !DIExpression(), !99)
    #dbg_declare(ptr %local_data, !100, !DIExpression(), !105)
  call void @llvm.memset.p0.i64(ptr align 4 %local_data, i8 0, i64 40, i1 false), !dbg !105
  %0 = load i32, ptr %idx.addr, align 4, !dbg !106
  %rem = srem i32 %0, 10, !dbg !107
  %idxprom = sext i32 %rem to i64, !dbg !108
  %arrayidx = getelementptr inbounds [10 x float], ptr %local_data, i64 0, i64 %idxprom, !dbg !108
  call void @__afl_report_array(i32 -871682864, i64 %idxprom, i32 2, i32 15), !dbg !109
  store float 1.500000e+00, ptr %arrayidx, align 4, !dbg !109
  ret void, !dbg !110
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #2

; Function Attrs: noinline nounwind uwtable
define dso_local void @test_multidimensional_array(i32 noundef %r, i32 noundef %c) #0 !dbg !111 {
entry:
  %r.addr = alloca i32, align 4
  %c.addr = alloca i32, align 4
  %matrix = alloca [3 x [4 x i8]], align 1
  store i32 %r, ptr %r.addr, align 4
    #dbg_declare(ptr %r.addr, !114, !DIExpression(), !115)
  store i32 %c, ptr %c.addr, align 4
    #dbg_declare(ptr %c.addr, !116, !DIExpression(), !117)
    #dbg_declare(ptr %matrix, !118, !DIExpression(), !122)
  call void @llvm.memset.p0.i64(ptr align 1 %matrix, i8 0, i64 12, i1 false), !dbg !122
  %0 = load i32, ptr %r.addr, align 4, !dbg !123
  %rem = srem i32 %0, 3, !dbg !124
  %idxprom = sext i32 %rem to i64, !dbg !125
  %arrayidx = getelementptr inbounds [3 x [4 x i8]], ptr %matrix, i64 0, i64 %idxprom, !dbg !125
  call void @__afl_report_array(i32 -871666480, i64 %idxprom, i32 0, i32 21), !dbg !126
  %1 = load i32, ptr %c.addr, align 4, !dbg !126
  %rem1 = srem i32 %1, 4, !dbg !127
  %idxprom2 = sext i32 %rem1 to i64, !dbg !125
  %arrayidx3 = getelementptr inbounds [4 x i8], ptr %arrayidx, i64 0, i64 %idxprom2, !dbg !125
  call void @__afl_report_array(i32 -871656672, i64 %idxprom2, i32 2, i32 21), !dbg !128
  store i8 65, ptr %arrayidx3, align 1, !dbg !128
  %2 = load i32, ptr %r.addr, align 4, !dbg !129
  %rem4 = srem i32 %2, 3, !dbg !130
  %idxprom5 = sext i32 %rem4 to i64, !dbg !131
  %arrayidx6 = getelementptr inbounds [3 x [4 x i8]], ptr %matrix, i64 0, i64 %idxprom5, !dbg !131
  call void @__afl_report_array(i32 -871666480, i64 %idxprom5, i32 0, i32 22), !dbg !132
  %3 = load i32, ptr %c.addr, align 4, !dbg !132
  %rem7 = srem i32 %3, 4, !dbg !133
  %idxprom8 = sext i32 %rem7 to i64, !dbg !131
  %arrayidx9 = getelementptr inbounds [4 x i8], ptr %arrayidx6, i64 0, i64 %idxprom8, !dbg !131
  call void @__afl_report_array(i32 -871649104, i64 %idxprom8, i32 1, i32 22), !dbg !131
  %4 = load i8, ptr %arrayidx9, align 1, !dbg !131
  %conv = zext i8 %4 to i32, !dbg !131
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str.4, i32 noundef %conv), !dbg !134
  ret void, !dbg !135
}

; Function Attrs: noinline nounwind uwtable
define dso_local void @test_struct_array(i32 noundef %idx) #0 !dbg !136 {
entry:
  %idx.addr = alloca i32, align 4
  %packet = alloca %struct.DataPacket, align 4
  store i32 %idx, ptr %idx.addr, align 4
    #dbg_declare(ptr %idx.addr, !137, !DIExpression(), !138)
    #dbg_declare(ptr %packet, !139, !DIExpression(), !147)
  call void @llvm.memset.p0.i64(ptr align 4 %packet, i8 0, i64 36, i1 false), !dbg !147
  %payload = getelementptr inbounds %struct.DataPacket, ptr %packet, i32 0, i32 1, !dbg !148
  call void @__afl_report_array(i32 -871643424, i64 1, i32 0, i32 33), !dbg !149
  %0 = load i32, ptr %idx.addr, align 4, !dbg !149
  %rem = srem i32 %0, 32, !dbg !150
  %idxprom = sext i32 %rem to i64, !dbg !151
  %arrayidx = getelementptr inbounds [32 x i8], ptr %payload, i64 0, i64 %idxprom, !dbg !151
  call void @__afl_report_array(i32 -871637632, i64 %idxprom, i32 2, i32 33), !dbg !152
  store i8 72, ptr %arrayidx, align 1, !dbg !152
  %payload1 = getelementptr inbounds %struct.DataPacket, ptr %packet, i32 0, i32 1, !dbg !153
  call void @__afl_report_array(i32 -871643424, i64 1, i32 0, i32 34), !dbg !154
  %1 = load i32, ptr %idx.addr, align 4, !dbg !154
  %rem2 = srem i32 %1, 32, !dbg !155
  %idxprom3 = sext i32 %rem2 to i64, !dbg !156
  %arrayidx4 = getelementptr inbounds [32 x i8], ptr %payload1, i64 0, i64 %idxprom3, !dbg !156
  call void @__afl_report_array(i32 -871633392, i64 %idxprom3, i32 1, i32 34), !dbg !156
  %2 = load i8, ptr %arrayidx4, align 1, !dbg !156
  %conv = zext i8 %2 to i32, !dbg !156
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str.5, i32 noundef %conv), !dbg !157
  ret void, !dbg !158
}

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 !dbg !159 {
entry:
  %retval = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str.6), !dbg !162
  call void @test_array_operations(i32 noundef 2), !dbg !163
  call void @test_multidimensional_array(i32 noundef 1, i32 noundef 2), !dbg !164
  call void @test_struct_array(i32 noundef 0), !dbg !165
  %call1 = call i32 (ptr, ...) @printf(ptr noundef @.str.7), !dbg !166
  ret i32 0, !dbg !167
}

attributes #0 = { noinline nounwind uwtable "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #1 = { "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: write) }

!llvm.dbg.cu = !{!24}
!llvm.module.flags = !{!49, !50, !51, !52, !53, !54, !55}
!llvm.ident = !{!56}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 5, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "test.c", directory: "/src/my_pass", checksumkind: CSK_MD5, checksum: "4d7265ad4a699e5b494ad1d847d16504")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 40, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_unsigned_char)
!5 = !{!6}
!6 = !DISubrange(count: 5)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(scope: null, file: !2, line: 5, type: !9, isLocal: true, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 48, elements: !10)
!10 = !{!11}
!11 = !DISubrange(count: 6)
!12 = !DIGlobalVariableExpression(var: !13, expr: !DIExpression())
!13 = distinct !DIGlobalVariable(scope: null, file: !2, line: 5, type: !14, isLocal: true, isDefinition: true)
!14 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 32, elements: !15)
!15 = !{!16}
!16 = !DISubrange(count: 4)
!17 = !DIGlobalVariableExpression(var: !18, expr: !DIExpression())
!18 = distinct !DIGlobalVariable(scope: null, file: !2, line: 6, type: !19, isLocal: true, isDefinition: true)
!19 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 544, elements: !20)
!20 = !{!21}
!21 = !DISubrange(count: 68)
!22 = !DIGlobalVariableExpression(var: !23, expr: !DIExpression())
!23 = distinct !DIGlobalVariable(name: "global_scores", scope: !24, file: !2, line: 11, type: !47, isLocal: false, isDefinition: true)
!24 = distinct !DICompileUnit(language: DW_LANG_C11, file: !25, producer: "Ubuntu clang version 19.1.1 (1ubuntu1~24.04.2)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: !26, splitDebugInlining: false, nameTableKind: None)
!25 = !DIFile(filename: "/src/my_pass/test.c", directory: "/src/my_pass/build", checksumkind: CSK_MD5, checksum: "4d7265ad4a699e5b494ad1d847d16504")
!26 = !{!0, !7, !12, !17, !22, !27, !32, !37, !42}
!27 = !DIGlobalVariableExpression(var: !28, expr: !DIExpression())
!28 = distinct !DIGlobalVariable(scope: null, file: !2, line: 22, type: !29, isLocal: true, isDefinition: true)
!29 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 128, elements: !30)
!30 = !{!31}
!31 = !DISubrange(count: 16)
!32 = !DIGlobalVariableExpression(var: !33, expr: !DIExpression())
!33 = distinct !DIGlobalVariable(scope: null, file: !2, line: 34, type: !34, isLocal: true, isDefinition: true)
!34 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 160, elements: !35)
!35 = !{!36}
!36 = !DISubrange(count: 20)
!37 = !DIGlobalVariableExpression(var: !38, expr: !DIExpression())
!38 = distinct !DIGlobalVariable(scope: null, file: !2, line: 38, type: !39, isLocal: true, isDefinition: true)
!39 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 376, elements: !40)
!40 = !{!41}
!41 = !DISubrange(count: 47)
!42 = !DIGlobalVariableExpression(var: !43, expr: !DIExpression())
!43 = distinct !DIGlobalVariable(scope: null, file: !2, line: 42, type: !44, isLocal: true, isDefinition: true)
!44 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 176, elements: !45)
!45 = !{!46}
!46 = !DISubrange(count: 22)
!47 = !DICompositeType(tag: DW_TAG_array_type, baseType: !48, size: 160, elements: !5)
!48 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!49 = !{i32 7, !"Dwarf Version", i32 5}
!50 = !{i32 2, !"Debug Info Version", i32 3}
!51 = !{i32 1, !"wchar_size", i32 4}
!52 = !{i32 8, !"PIC Level", i32 2}
!53 = !{i32 7, !"PIE Level", i32 2}
!54 = !{i32 7, !"uwtable", i32 2}
!55 = !{i32 7, !"frame-pointer", i32 1}
!56 = !{!"Ubuntu clang version 19.1.1 (1ubuntu1~24.04.2)"}
!57 = distinct !DISubprogram(name: "__afl_report_array", scope: !2, file: !2, line: 4, type: !58, scopeLine: 4, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !24, retainedNodes: !71)
!58 = !DISubroutineType(types: !59)
!59 = !{null, !60, !65, !69, !69}
!60 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint32_t", file: !61, line: 26, baseType: !62)
!61 = !DIFile(filename: "/usr/include/aarch64-linux-gnu/bits/stdint-uintn.h", directory: "", checksumkind: CSK_MD5, checksum: "256fcabbefa27ca8cf5e6d37525e6e16")
!62 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint32_t", file: !63, line: 42, baseType: !64)
!63 = !DIFile(filename: "/usr/include/aarch64-linux-gnu/bits/types.h", directory: "", checksumkind: CSK_MD5, checksum: "e1865d9fe29fe1b5ced550b7ba458f9e")
!64 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!65 = !DIDerivedType(tag: DW_TAG_typedef, name: "int64_t", file: !66, line: 27, baseType: !67)
!66 = !DIFile(filename: "/usr/include/aarch64-linux-gnu/bits/stdint-intn.h", directory: "", checksumkind: CSK_MD5, checksum: "649b383a60bfa3eb90e85840b2b0be20")
!67 = !DIDerivedType(tag: DW_TAG_typedef, name: "__int64_t", file: !63, line: 44, baseType: !68)
!68 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!69 = !DIDerivedType(tag: DW_TAG_typedef, name: "int32_t", file: !66, line: 26, baseType: !70)
!70 = !DIDerivedType(tag: DW_TAG_typedef, name: "__int32_t", file: !63, line: 41, baseType: !48)
!71 = !{}
!72 = !DILocalVariable(name: "id", arg: 1, scope: !57, file: !2, line: 4, type: !60)
!73 = !DILocation(line: 4, column: 34, scope: !57)
!74 = !DILocalVariable(name: "index", arg: 2, scope: !57, file: !2, line: 4, type: !65)
!75 = !DILocation(line: 4, column: 46, scope: !57)
!76 = !DILocalVariable(name: "type", arg: 3, scope: !57, file: !2, line: 4, type: !69)
!77 = !DILocation(line: 4, column: 61, scope: !57)
!78 = !DILocalVariable(name: "line", arg: 4, scope: !57, file: !2, line: 4, type: !69)
!79 = !DILocation(line: 4, column: 75, scope: !57)
!80 = !DILocalVariable(name: "mode", scope: !57, file: !2, line: 5, type: !81)
!81 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !82, size: 64)
!82 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!83 = !DILocation(line: 5, column: 17, scope: !57)
!84 = !DILocation(line: 5, column: 25, scope: !57)
!85 = !DILocation(line: 5, column: 30, scope: !57)
!86 = !DILocation(line: 5, column: 24, scope: !57)
!87 = !DILocation(line: 5, column: 48, scope: !57)
!88 = !DILocation(line: 5, column: 53, scope: !57)
!89 = !DILocation(line: 7, column: 13, scope: !57)
!90 = !DILocation(line: 7, column: 17, scope: !57)
!91 = !DILocation(line: 7, column: 23, scope: !57)
!92 = !DILocation(line: 7, column: 29, scope: !57)
!93 = !DILocation(line: 6, column: 5, scope: !57)
!94 = !DILocation(line: 8, column: 1, scope: !57)
!95 = distinct !DISubprogram(name: "test_array_operations", scope: !2, file: !2, line: 13, type: !96, scopeLine: 13, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !24, retainedNodes: !71)
!96 = !DISubroutineType(types: !97)
!97 = !{null, !48}
!98 = !DILocalVariable(name: "idx", arg: 1, scope: !95, file: !2, line: 13, type: !48)
!99 = !DILocation(line: 13, column: 32, scope: !95)
!100 = !DILocalVariable(name: "local_data", scope: !95, file: !2, line: 14, type: !101)
!101 = !DICompositeType(tag: DW_TAG_array_type, baseType: !102, size: 320, elements: !103)
!102 = !DIBasicType(name: "float", size: 32, encoding: DW_ATE_float)
!103 = !{!104}
!104 = !DISubrange(count: 10)
!105 = !DILocation(line: 14, column: 11, scope: !95)
!106 = !DILocation(line: 15, column: 16, scope: !95)
!107 = !DILocation(line: 15, column: 20, scope: !95)
!108 = !DILocation(line: 15, column: 5, scope: !95)
!109 = !DILocation(line: 15, column: 26, scope: !95)
!110 = !DILocation(line: 16, column: 1, scope: !95)
!111 = distinct !DISubprogram(name: "test_multidimensional_array", scope: !2, file: !2, line: 18, type: !112, scopeLine: 18, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !24, retainedNodes: !71)
!112 = !DISubroutineType(types: !113)
!113 = !{null, !48, !48}
!114 = !DILocalVariable(name: "r", arg: 1, scope: !111, file: !2, line: 18, type: !48)
!115 = !DILocation(line: 18, column: 38, scope: !111)
!116 = !DILocalVariable(name: "c", arg: 2, scope: !111, file: !2, line: 18, type: !48)
!117 = !DILocation(line: 18, column: 45, scope: !111)
!118 = !DILocalVariable(name: "matrix", scope: !111, file: !2, line: 20, type: !119)
!119 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 96, elements: !120)
!120 = !{!121, !16}
!121 = !DISubrange(count: 3)
!122 = !DILocation(line: 20, column: 10, scope: !111)
!123 = !DILocation(line: 21, column: 12, scope: !111)
!124 = !DILocation(line: 21, column: 14, scope: !111)
!125 = !DILocation(line: 21, column: 5, scope: !111)
!126 = !DILocation(line: 21, column: 19, scope: !111)
!127 = !DILocation(line: 21, column: 21, scope: !111)
!128 = !DILocation(line: 21, column: 26, scope: !111)
!129 = !DILocation(line: 22, column: 39, scope: !111)
!130 = !DILocation(line: 22, column: 41, scope: !111)
!131 = !DILocation(line: 22, column: 32, scope: !111)
!132 = !DILocation(line: 22, column: 46, scope: !111)
!133 = !DILocation(line: 22, column: 48, scope: !111)
!134 = !DILocation(line: 22, column: 5, scope: !111)
!135 = !DILocation(line: 23, column: 1, scope: !111)
!136 = distinct !DISubprogram(name: "test_struct_array", scope: !2, file: !2, line: 30, type: !96, scopeLine: 30, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !24, retainedNodes: !71)
!137 = !DILocalVariable(name: "idx", arg: 1, scope: !136, file: !2, line: 30, type: !48)
!138 = !DILocation(line: 30, column: 28, scope: !136)
!139 = !DILocalVariable(name: "packet", scope: !136, file: !2, line: 32, type: !140)
!140 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "DataPacket", file: !2, line: 25, size: 288, elements: !141)
!141 = !{!142, !143}
!142 = !DIDerivedType(tag: DW_TAG_member, name: "id", scope: !140, file: !2, line: 26, baseType: !48, size: 32)
!143 = !DIDerivedType(tag: DW_TAG_member, name: "payload", scope: !140, file: !2, line: 27, baseType: !144, size: 256, offset: 32)
!144 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 256, elements: !145)
!145 = !{!146}
!146 = !DISubrange(count: 32)
!147 = !DILocation(line: 32, column: 23, scope: !136)
!148 = !DILocation(line: 33, column: 12, scope: !136)
!149 = !DILocation(line: 33, column: 20, scope: !136)
!150 = !DILocation(line: 33, column: 24, scope: !136)
!151 = !DILocation(line: 33, column: 5, scope: !136)
!152 = !DILocation(line: 33, column: 30, scope: !136)
!153 = !DILocation(line: 34, column: 43, scope: !136)
!154 = !DILocation(line: 34, column: 51, scope: !136)
!155 = !DILocation(line: 34, column: 55, scope: !136)
!156 = !DILocation(line: 34, column: 36, scope: !136)
!157 = !DILocation(line: 34, column: 5, scope: !136)
!158 = !DILocation(line: 35, column: 1, scope: !136)
!159 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 37, type: !160, scopeLine: 37, spFlags: DISPFlagDefinition, unit: !24)
!160 = !DISubroutineType(types: !161)
!161 = !{!48}
!162 = !DILocation(line: 38, column: 5, scope: !159)
!163 = !DILocation(line: 39, column: 5, scope: !159)
!164 = !DILocation(line: 40, column: 5, scope: !159)
!165 = !DILocation(line: 41, column: 5, scope: !159)
!166 = !DILocation(line: 42, column: 5, scope: !159)
!167 = !DILocation(line: 43, column: 5, scope: !159)
