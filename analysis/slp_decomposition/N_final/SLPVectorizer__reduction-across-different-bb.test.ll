@c = global ptr null
@e = global ptr null
@a = external global i16
@f = external global i16
define void @test(ptr %j, ptr %b) {
;
entry:
  %scevgep5 = getelementptr i8, ptr %b, i16 8
  %scevgep6 = getelementptr i8, ptr %j, i16 4
  %bound038 = icmp ult ptr @a, getelementptr inbounds nuw (i8, ptr @f, i16 1)
  %bound139 = icmp ult ptr @f, getelementptr inbounds nuw (i8, ptr @a, i16 1)
  %found.conflict40 = and i1 %bound038, %bound139
  %bound042 = icmp ugt ptr %scevgep5, @a
  %bound143 = icmp ult ptr %b, getelementptr inbounds nuw (i8, ptr @a, i16 1)
  %found.conflict44 = and i1 %bound042, %bound143
  %invariant.op = or i1 %found.conflict40, %found.conflict44
  %bound046 = icmp ugt ptr %scevgep6, @a
  %bound147 = icmp ult ptr %j, getelementptr inbounds nuw (i8, ptr @a, i16 1)
  %found.conflict48 = and i1 %bound046, %bound147
  %invariant.op58 = or i1 %invariant.op, %found.conflict48
  %bound050 = icmp ugt ptr %scevgep5, @f
  %bound151 = icmp ult ptr %b, getelementptr inbounds nuw (i8, ptr @f, i16 1)
  %found.conflict52 = and i1 %bound050, %bound151
  %invariant.op60 = or i1 %invariant.op58, %found.conflict52
  br label %loop

loop:
  %0 = load ptr, ptr @c, align 1
  %1 = load ptr, ptr @e, align 1
  %scevgep = getelementptr i8, ptr %0, i16 1
  %scevgep4 = getelementptr i8, ptr %1, i16 4
  %bound0 = icmp ult ptr %0, %scevgep4
  %bound1 = icmp ult ptr %1, %scevgep
  %found.conflict = and i1 %bound0, %bound1
  %bound07 = icmp ult ptr %0, getelementptr inbounds nuw (i8, ptr @a, i16 1)
  %bound18 = icmp ugt ptr %scevgep, @a
  %found.conflict9 = and i1 %bound07, %bound18
  %conflict.rdx = or i1 %found.conflict, %found.conflict9
  %bound010 = icmp ult ptr %0, getelementptr inbounds nuw (i8, ptr @f, i16 1)
  %bound111 = icmp ugt ptr %scevgep, @f
  %found.conflict12 = and i1 %bound010, %bound111
  %conflict.rdx13 = or i1 %conflict.rdx, %found.conflict12
  %bound014 = icmp ult ptr %0, %scevgep5
  %bound115 = icmp ult ptr %b, %scevgep
  %found.conflict16 = and i1 %bound014, %bound115
  %conflict.rdx17 = or i1 %conflict.rdx13, %found.conflict16
  %bound022 = icmp ult ptr %1, getelementptr inbounds nuw (i8, ptr @a, i16 1)
  %bound123 = icmp ugt ptr %scevgep4, @a
  %found.conflict24 = and i1 %bound022, %bound123
  %conflict.rdx25 = or i1 %conflict.rdx17, %found.conflict24
  %bound026 = icmp ult ptr %1, getelementptr inbounds nuw (i8, ptr @f, i16 1)
  %bound127 = icmp ugt ptr %scevgep4, @f
  %found.conflict28 = and i1 %bound026, %bound127
  %conflict.rdx29 = or i1 %conflict.rdx25, %found.conflict28
  %bound030 = icmp ult ptr %1, %scevgep5
  %bound131 = icmp ult ptr %b, %scevgep4
  %found.conflict32 = and i1 %bound030, %bound131
  %conflict.rdx33 = or i1 %conflict.rdx29, %found.conflict32
  %conflict.rdx57.reass = or i1 %conflict.rdx33, %invariant.op60
  br i1 %conflict.rdx57.reass, label %loop, label %exit

exit:
  ret void
}
