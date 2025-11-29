extends Reference
# test 2.1.6

export var test_export = "test_export"
onready var test_onready = "test_onready"


signal test_signal(arg1, arg2)


func test_constants():
	var a = 1 # constant
	var b = 2.15 # constant
	var c = "hello" # constant
	var d = Vector2(1, 2) # constant
	var e = Rect2(1, 2, 3, 4) # constant
	var f = Vector3(1, 2, 3) # constant
	var g = Matrix32(Vector2(1, 2), Vector2(3, 4), Vector2(5, 6)) # constant
	var h = Plane(1, 2, 3, 4) # constant
	var i = Quat(1, 2, 3, 4) # constant
	var j = AABB(Vector3(1, 2, 3), Vector3(4, 5, 6)) # constant
	var k = Transform(Vector3(1, 2, 3), Vector3(4, 5, 6), Vector3(7, 8, 9), Vector3(7, 8, 9)) # constant
	var l = Color(1, 2, 3, 4) # constant
	var m = Image() # constant
	var n = NodePath("foo/bar") # constant
	var o = RID() # constant
	var p = Object() # constant
	var q = InputEvent() # constant
	var r = Dictionary({ "a": 1, "b": 2 }) # constant
	var s = Array([1, 2, 3]) # constant
	var t = RawArray([1, 2, 3]) # constant
	var u = IntArray([1, 2, 3]) # constant
	var v = FloatArray([1, 2, 3]) # constant
	var w = StringArray(["hello", "world"]) # constant
	var x = Vector2Array([Vector2(1, 2), Vector2(3, 4)]) # constant
	var y = Vector3Array([Vector3(1, 2, 3), Vector3(4, 5, 6)]) # constant
	var z = ColorArray([Color(1, 2, 3, 4), Color(5, 6, 7, 8)]) # constant

func test_functions():
	sin(1)
	cos(1)
	tan(1)
	sinh(1)
	cosh(1)
	tanh(1)
	asin(1)
	acos(1)
	atan(1)
	atan2(1, 2)
	sqrt(1)
	fmod(1, 2)
	fposmod(1, 2)
	floor(1)
	ceil(1)
	round(1)
	abs(1)
	sign(1)
	pow(1, 2)
	log(1)
	exp(1)
	is_nan(1)
	is_inf(1)
	ease(1, 2)
	decimals(1)
	stepify(1, 2)
	lerp(1, 2, 3)
	dectime(1, 2, 3)
	randomize()
	randi()
	randf()
	rand_range(1, 2)
	seed(1)
	rand_seed(1)
	deg2rad(1)
	rad2deg(1)
	linear2db(1)
	db2linear(1)
	max(1, 2)
	min(1, 2)
	clamp(1, 2, 3)
	nearest_po2(1)
	weakref(self)
	funcref(self, "test_constants")
	convert(1, 2)
	typeof(1)
	type_exists(1)
	str(1)
	print(1)
	printt(1)
	prints(1)
	printerr(1)
	printraw(1)
	var2str(1)
	str2var("1")
	var conv = var2bytes(1)
	bytes2var(conv)
	range(1, 2, 3)
	load("res://test.gd")
	var dict = inst2dict(self)
	dict2inst(dict)
	hash(1)
	Color8(1, 2, 3, 4)
	ColorN("RED", 0.1)
	print_stack()
	instance_from_id(1)
	pass

enum TestEnum {
	FOO
	BAR
}
enum {TEST_ENUM_VALUE_1, TEST_ENUM_VALUE_2}


class InnerTestClass:
	func foo():
		pass

func test_tokens():
	# IN
	var i = int(TEST_ENUM_VALUE_1)
	for i in range(10):
		print(i)
	# EQUAL
	if i == 10:
		print("i is 10")
	# NOT_EQUAL
	if i != 10:
		print("i is not 10")
	# LESS
	if i < 10:
		print("i is less than 10")
	# GREATER
	if i > 10:
		print("i is greater than 10")
	# LESS_EQUAL
	if i <= 10:
		print("i is less than or equal to 10")
	# GREATER_EQUAL
	if i >= 10:
		print("i is greater than or equal to 10")
	# AND
	if i == 10 and i == 10:
		print("i is 10 and i is 10")
	# OR
	if i == 10 or i == 11:
		print("i is 10 or i is 11")
	# NOT
	if not i == 10:
		print("i is not 10")

	# NON_ASSIGN
	# ADD
	i + 1
	# SUB
	i - 1
	# MUL
	i * 1
	# DIV
	i / 1
	# MOD
	i % 1
	# SHIFT_LEFT
	i << 1
	# SHIFT_RIGHT
	i >> 1
	# BIT_AND
	i & 1
	# BIT_OR
	i | 1
	# BIT_XOR
	i ^ 1
	# BIT_INVERT
	i = ~i
	# CLASS
	# EXTENDS
	# IF

	# ASSIGN
	# ADD
	i += 1
	# SUB
	i -= 1
	# MUL
	i *= 1
	# DIV
	i /= 1
	# MOD
	i %= 1
	# SHIFT_LEFT
	i <<= 1
	# SHIFT_RIGHT
	i >>= 1
	# BIT_AND
	i &= 1
	# BIT_OR
	i |= 1
	# BIT_XOR
	i ^= 1
	# BIT_INVERT
	i = ~i
	# CLASS
	# EXTENDS
	# IF
	if i == 10:
		print("i is 10")
	# ELIF
	elif i == 11:
		print("i is 11")
	# ELSE
	else:
		print("i is not 10")
	# FOR, IN
	for i in range(10):
		print(i)


	# WHILE
	while i < 10:
		print(i)
		i += 1
		break
	while i > 10:
		continue # CONTINUE
	# DO (doesn't actually parse in 2.1.6)
	# do:
	# 	print("i is less than 10")
	# 	i += 1
	# SWITCH
	# CASE
	# these don't parse either
	#PASS
	pass
	# ASSERT
	assert(1 == 1)
	# BREAKPOINT
	breakpoint
	#semicolon
	print("test"); print("test2")
	# PERIOD
	InnerTestClass.new().foo()
	# QUESTION_MARK (Doesn't actually parse in 2.1.6)
	#var test_question_mark = 1 ? 2 : 3
	# CONST_PI
	PI
	yield()
	return true
