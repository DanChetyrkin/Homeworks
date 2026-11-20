(* the binary tree type from the question *)
(* a tree is empty or a node with a value, a left tree and a right tree *)
datatype 'a binary_tree =
    Empty
  | Node of 'a * 'a binary_tree * 'a binary_tree;

(* add_to_search_tree gets a search tree and an int x *)
(* it returns a new search tree with x inside *)
(* if the tree is empty we make a new node with x and two empty trees *)
fun add_to_search_tree Empty (x : int) = Node (x, Empty, Empty)
  (* if x is smaller than the node value we add x to the left tree *)
  (* if not we add x to the right tree *)
  | add_to_search_tree (Node (v, left, right)) x =
      if x < v then Node (v, add_to_search_tree left x, right)
      else Node (v, left, add_to_search_tree right x);

(* part b answer: *)
(* to make it work for float numbers we only change int to real *)
(* in the first line of the function: (x : real) instead of (x : int) *)
(* the < sign works for int and for real, but if we do not say the type *)
(* sml thinks it is int, so we must write real to get a real tree *)
(* we do not use = to compare values, so real works fine here *)
(* (in sml real is not a type you can check with =) *)
(* the new function will be: *)
(* fun add_to_search_tree Empty (x : real) = Node (x, Empty, Empty) *)
(*   | add_to_search_tree (Node (v, left, right)) x = *)
(*       if x < v then Node (v, add_to_search_tree left x, right) *)
(*       else Node (v, left, add_to_search_tree right x); *)
