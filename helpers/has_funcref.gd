extends Object

func foo():
	return ("bar")

func test():
	var a = funcref(self, "foo")
	range(1, 10, 1)
	return 100