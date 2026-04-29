target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-ios5.0.0"
define void @select_smax_8xi16(ptr %ptr, i16 %x) {
;
entry:
  %l.0 = load i16, ptr %ptr
  %cmp.0 = icmp sgt i16 %l.0, 16383
  %s.0 = select i1 %cmp.0, i16 %l.0, i16 16383
  store i16 %s.0, ptr %ptr, align 2

  %gep.1 = getelementptr inbounds i16, ptr %ptr, i16 1
  %l.1 = load i16, ptr %gep.1
  %cmp.1 = icmp sgt i16 %l.1, 16383
  %s.1 = select i1 %cmp.1, i16 %l.1, i16 16383
  store i16 %s.1, ptr %gep.1, align 2

  %gep.2 = getelementptr inbounds i16, ptr %ptr, i16 2
  %l.2 = load i16, ptr %gep.2
  %cmp.2 = icmp sgt i16 %l.2, 16383
  %s.2 = select i1 %cmp.2, i16 %l.2, i16 16383
  store i16 %s.2, ptr %gep.2, align 2

  %gep.3 = getelementptr inbounds i16, ptr %ptr, i16 3
  %l.3 = load i16, ptr %gep.3
  %cmp.3 = icmp sgt i16 %l.3, 16383
  %s.3 = select i1 %cmp.3, i16 %l.3, i16 16383
  store i16 %s.3, ptr %gep.3, align 2

  %gep.4 = getelementptr inbounds i16, ptr %ptr, i16 4
  %l.4 = load i16, ptr %gep.4
  %cmp.4 = icmp sgt i16 %l.4, 16383
  %s.4 = select i1 %cmp.4, i16 %l.4, i16 16383
  store i16 %s.4, ptr %gep.4, align 2

  %gep.5 = getelementptr inbounds i16, ptr %ptr, i16 5
  %l.5 = load i16, ptr %gep.5
  %cmp.5 = icmp sgt i16 %l.5, 16383
  %s.5 = select i1 %cmp.5, i16 %l.5, i16 16383
  store i16 %s.5, ptr %gep.5, align 2

  %gep.6 = getelementptr inbounds i16, ptr %ptr, i16 6
  %l.6 = load i16, ptr %gep.6
  %cmp.6 = icmp sgt i16 %l.6, 16383
  %s.6 = select i1 %cmp.6, i16 %l.6, i16 16383
  store i16 %s.6, ptr %gep.6, align 2

  %gep.7 = getelementptr inbounds i16, ptr %ptr, i16 7
  %l.7 = load i16, ptr %gep.7
  %cmp.7 = icmp sgt i16 %l.7, 16383
  %s.7 = select i1 %cmp.7, i16 %l.7, i16 16383
  store i16 %s.7, ptr %gep.7, align 2
  ret void
}
